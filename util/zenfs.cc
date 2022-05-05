// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#include <dirent.h>
#include <fcntl.h>
#include <gflags/gflags.h>
#include <rocksdb/file_system.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <streambuf>

#ifdef WITH_TERARKDB
#include <fs/fs_zenfs.h>
#include <fs/version.h>
#else
#include <rocksdb/plugin/zenfs/fs/fs_zenfs.h>
#include <rocksdb/plugin/zenfs/fs/version.h>
#endif

using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using GFLAGS_NAMESPACE::RegisterFlagValidator;
using GFLAGS_NAMESPACE::SetUsageMessage;

DEFINE_string(zbd, "", "Path to a zoned block device.");
DEFINE_string(aux_path, "",
              "Path for auxiliary file storage (log and lock files).");
DEFINE_bool(
    force, false,
    "Force the action. May result in data loss.\n"
    "If used with mkfs or rmdir commands, data will be lost on an existing "
    "file system. If used with backup, data copied from "
    "the drive will likely be incomplete and/or corrupt "
    "- only use this for testing purposes.");
DEFINE_string(path, "", "File path");
DEFINE_int32(finish_threshold, 0, "Finish used zones if less than x% left");
DEFINE_string(restore_path, "", "Path to restore files");
DEFINE_string(backup_path, "", "Path to backup files");
DEFINE_string(src_file, "", "Source file path");
DEFINE_string(dest_file, "", "Destination file path");

namespace ROCKSDB_NAMESPACE {

void AddDirSeparatorAtEnd(std::string &path) {
  if (path.back() != '/') path = path + "/";
}

std::unique_ptr<ZonedBlockDevice> zbd_open(bool readonly, bool exclusive) {
  std::unique_ptr<ZonedBlockDevice> zbd{
      new ZonedBlockDevice(FLAGS_zbd, nullptr)};
  IOStatus open_status = zbd->Open(readonly, exclusive);

  if (!open_status.ok()) {
    fprintf(stderr, "Failed to open zoned block device: %s, error: %s\n",
            FLAGS_zbd.c_str(), open_status.ToString().c_str());
    zbd.reset();
  }

  return zbd;
}

// Here we pass 'zbd' by non-const reference to be able to pass its ownership
// to 'zenFS'
Status zenfs_mount(std::unique_ptr<ZonedBlockDevice> &zbd,
                   std::unique_ptr<ZenFS> *zenFS, bool readonly) {
  Status s;

  std::unique_ptr<ZenFS> localZenFS{
      new ZenFS(zbd.release(), FileSystem::Default(), nullptr)};
  s = localZenFS->Mount(readonly);
  if (!s.ok()) {
    localZenFS.reset();
  }
  *zenFS = std::move(localZenFS);

  return s;
}

int is_dir(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) {
    fprintf(stderr, "Failed to stat %s\n", path);
    return 1;
  }
  return S_ISDIR(st.st_mode);
}

// Create or check pre-existing aux directory and fail if it is
// inaccessible by current user and if it has previous data
int create_aux_dir(const char *path) {
  struct dirent *dent;
  size_t nfiles = 0;
  int ret = 0;

  errno = 0;
  ret = mkdir(path, 0750);
  if (ret < 0 && EEXIST != errno) {
    fprintf(stderr, "Failed to create aux directory %s: %s\n", path,
            strerror(errno));
    return 1;
  }
  // The aux_path is now available, check if it is a directory infact
  // and is empty and the user has access permission

  if (!is_dir(path)) {
    fprintf(stderr, "Aux path %s is not a directory\n", path);
    return 1;
  }

  errno = 0;

  auto closedirDeleter = [](DIR *d) {
    if (d != nullptr) closedir(d);
  };
  std::unique_ptr<DIR, decltype(closedirDeleter)> aux_dir{
      opendir(path), std::move(closedirDeleter)};
  if (errno) {
    fprintf(stderr, "Failed to open aux directory %s: %s\n", path,
            strerror(errno));
    return 1;
  }

  // Consider the directory as non-empty if any files/dir other
  // than . and .. are found.
  while ((dent = readdir(aux_dir.get())) != NULL && nfiles <= 2) ++nfiles;
  if (nfiles > 2) {
    fprintf(stderr, "Aux directory %s is not empty.\n", path);
    return 1;
  }

  if (access(path, R_OK | W_OK | X_OK) < 0) {
    fprintf(stderr,
            "User does not have access permissions on "
            "aux directory %s\n",
            path);
    return 1;
  }

  return 0;
}

int zenfs_tool_mkfs() {
  Status s;

  if (FLAGS_aux_path.empty()) {
    fprintf(stderr, "You need to specify --aux_path\n");
    return 1;
  }

  if (create_aux_dir(FLAGS_aux_path.c_str())) return 1;

  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(false, true);
  if (!zbd) return 1;

  std::unique_ptr<ZenFS> zenFS;
  s = zenfs_mount(zbd, &zenFS, false);
  if ((s.ok() || !s.IsNotFound()) && !FLAGS_force) {
    fprintf(
        stderr,
        "Existing filesystem found, use --force if you want to replace it.\n");
    return 1;
  }

  zenFS.reset();

  zbd = zbd_open(false, true);
  ZonedBlockDevice *zbdRaw = zbd.get();
  zenFS.reset(new ZenFS(zbd.release(), FileSystem::Default(), nullptr));

  if (FLAGS_aux_path.back() != '/') FLAGS_aux_path.append("/");

  s = zenFS->MkFS(FLAGS_aux_path, FLAGS_finish_threshold);
  if (!s.ok()) {
    fprintf(stderr, "Failed to create file system, error: %s\n",
            s.ToString().c_str());
    return 1;
  }

  fprintf(stdout, "ZenFS file system created. Free space: %lu MB\n",
          zbdRaw->GetFreeSpace() / (1024 * 1024));

  return 0;
}

void list_children(const std::unique_ptr<ZenFS> &zenFS,
                   const std::string &path) {
  IOOptions opts;
  IODebugContext dbg;
  std::vector<std::string> result;
  uint64_t size;
  IOStatus io_status = zenFS->GetChildren(path, opts, &result, &dbg);

  if (!io_status.ok()) {
    fprintf(stderr, "Error: %s %s\n", io_status.ToString().c_str(),
            path.c_str());
    return;
  }

  for (const auto &f : result) {
    io_status = zenFS->GetFileSize(path + f, opts, &size, &dbg);
    if (!io_status.ok()) {
      fprintf(stderr, "Failed to get size of file %s\n", f.c_str());
      return;
    }
    uint64_t mtime;
    io_status = zenFS->GetFileModificationTime(path + f, opts, &mtime, &dbg);
    if (!io_status.ok()) {
      fprintf(stderr,
              "Failed to get modification time of file %s, error = %s\n",
              f.c_str(), io_status.ToString().c_str());
      return;
    }
    time_t mt = (time_t)mtime;
    struct tm *fct = std::localtime(&mt);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%b %d %Y %H:%M:%S", fct);
    std::string mdtime;
    mdtime.assign(buf, sizeof(buf));

    fprintf(stdout, "%12lu\t%-32s%-32s\n", size, mdtime.c_str(), f.c_str());
  }
}

int zenfs_tool_list() {
  Status s;
  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(true, false);
  if (!zbd) return 1;

  std::unique_ptr<ZenFS> zenFS;
  s = zenfs_mount(zbd, &zenFS, true);
  if (!s.ok()) {
    fprintf(stderr, "Failed to mount filesystem, error: %s\n",
            s.ToString().c_str());
    return 1;
  }
  AddDirSeparatorAtEnd(FLAGS_path);
  list_children(zenFS, FLAGS_path);

  return 0;
}

int zenfs_tool_df() {
  Status s;
  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(true, false);
  if (!zbd) return 1;
  ZonedBlockDevice *zbdRaw = zbd.get();

  std::unique_ptr<ZenFS> zenFS;
  s = zenfs_mount(zbd, &zenFS, true);
  if (!s.ok()) {
    fprintf(stderr, "Failed to mount filesystem, error: %s\n",
            s.ToString().c_str());
    return 1;
  }
  uint64_t used = zbdRaw->GetUsedSpace();
  uint64_t free = zbdRaw->GetFreeSpace();
  uint64_t reclaimable = zbdRaw->GetReclaimableSpace();

  /* Avoid divide by zero */
  if (used == 0) used = 1;

  fprintf(stdout,
          "Free: %lu MB\nUsed: %lu MB\nReclaimable: %lu MB\nSpace "
          "amplification: %lu%%\n",
          free / (1024 * 1024), used / (1024 * 1024),
          reclaimable / (1024 * 1024), (100 * reclaimable) / used);

  return 0;
}

int zenfs_tool_lsuuid() {
  std::map<std::string, std::string> zenFileSystems;
  Status s = ListZenFileSystems(zenFileSystems);
  if (!s.ok()) {
    fprintf(stderr, "Failed to enumerate file systems: %s",
            s.ToString().c_str());
    return 1;
  }

  for (const auto &p : zenFileSystems)
    fprintf(stdout, "%s\t%s\n", p.first.c_str(), p.second.c_str());

  return 0;
}

static std::map<std::string, Env::WriteLifeTimeHint> wlth_map;

Env::WriteLifeTimeHint GetWriteLifeTimeHint(const std::string &filename) {
  if (wlth_map.find(filename) != wlth_map.end()) {
    return wlth_map[filename];
  }
  return Env::WriteLifeTimeHint::WLTH_NOT_SET;
}

int SaveWriteLifeTimeHints() {
  std::ofstream wlth_file(FLAGS_path + "/write_lifetime_hints.dat");

  if (!wlth_file.is_open()) {
    fprintf(stderr, "Failed to store time hints\n");
    return 1;
  }

  for (auto it = wlth_map.begin(); it != wlth_map.end(); it++) {
    wlth_file << it->first << "\t" << it->second << "\n";
  }

  wlth_file.close();
  return 0;
}

void ReadWriteLifeTimeHints() {
  std::ifstream wlth_file(FLAGS_path + "/write_lifetime_hints.dat");

  if (!wlth_file.is_open()) {
    fprintf(stderr, "WARNING: failed to read write life times\n");
    return;
  }

  std::string filename;
  uint32_t lth;

  while (wlth_file >> filename >> lth) {
    wlth_map.insert(std::make_pair(filename, (Env::WriteLifeTimeHint)lth));
  }

  wlth_file.close();
}

IOStatus zenfs_tool_copy_file(FileSystem *f_fs, const std::string &f,
                              FileSystem *t_fs, const std::string &t) {
  FileOptions fopts;
  IOOptions iopts;
  IODebugContext dbg;
  IOStatus s;
  std::unique_ptr<FSSequentialFile> f_file;
  std::unique_ptr<FSWritableFile> t_file;
  size_t buffer_sz = 1024 * 1024;
  uint64_t to_copy;

  fprintf(stdout, "%s\n", f.c_str());

  s = f_fs->GetFileSize(f, iopts, &to_copy, &dbg);
  if (!s.ok()) {
    return s;
  }

  s = f_fs->NewSequentialFile(f, fopts, &f_file, &dbg);
  if (!s.ok()) {
    return s;
  }

  s = t_fs->NewWritableFile(t, fopts, &t_file, &dbg);
  if (!s.ok()) {
    return s;
  }

  t_file->SetWriteLifeTimeHint(GetWriteLifeTimeHint(t));

  std::unique_ptr<char[]> buffer{new (std::nothrow) char[buffer_sz]};
  if (!buffer) {
    return IOStatus::IOError("Failed to allocate copy buffer");
  }

  while (to_copy > 0) {
    size_t chunk_sz = to_copy;
    Slice chunk_slice;

    if (chunk_sz > buffer_sz) chunk_sz = buffer_sz;

    s = f_file->Read(chunk_sz, iopts, &chunk_slice, buffer.get(), &dbg);
    if (!s.ok()) {
      break;
    }

    s = t_file->Append(chunk_slice, iopts, &dbg);
    to_copy -= chunk_slice.size();
  }

  if (!s.ok()) {
    return s;
  }

  return t_file->Fsync(iopts, &dbg);
}

IOStatus zenfs_tool_copy_dir(FileSystem *f_fs, const std::string &f_dir,
                             FileSystem *t_fs, const std::string &t_dir) {
  IOOptions opts;
  IODebugContext dbg;
  IOStatus s;
  std::vector<std::string> files;

  s = f_fs->GetChildren(f_dir, opts, &files, &dbg);
  if (!s.ok()) {
    return s;
  }

  for (const auto &f : files) {
    std::string filename = f_dir + f;
    bool is_dir;

    if (f == "." || f == ".." || f == "write_lifetime_hints.dat") continue;

    s = f_fs->IsDirectory(filename, opts, &is_dir, &dbg);
    if (!s.ok()) {
      return s;
    }

    std::string dest_filename;

    if (t_dir == "") {
      dest_filename = f;
    } else {
      if (t_dir.back() == '/') {
        dest_filename = t_dir + f;
      } else {
        dest_filename = t_dir + "/" + f;
      }
    }

    if (is_dir) {
      s = t_fs->CreateDir(dest_filename, opts, &dbg);
      if (!s.ok()) {
        return s;
      }
      s = zenfs_tool_copy_dir(f_fs, filename + "/", t_fs, dest_filename);
      if (!s.ok()) {
        return s;
      }
    } else {
      s = zenfs_tool_copy_file(f_fs, filename, t_fs, dest_filename);
      if (!s.ok()) {
        return s;
      }
    }
  }

  return s;
}
IOStatus zenfs_create_directories(FileSystem *fs, std::string path) {
  std::string dir_name;
  IODebugContext dbg;
  IOOptions opts;
  IOStatus s;
  std::size_t p = 0;

  if (path.back() != '/') path += '/';

  while ((p = path.find_first_of('/', p)) != std::string::npos) {
    dir_name = path.substr(0, p++);
    if (dir_name.size() == 0) continue;
    s = fs->CreateDirIfMissing(dir_name, opts, &dbg);
    if (!s.ok()) break;
  }

  return s;
}

int zenfs_tool_backup() {
  Status status;
  IOStatus io_status;
  IOOptions opts;
  IODebugContext dbg;
  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(true, true);

  if (!zbd) {
    if (FLAGS_force) {
      fprintf(stderr,
              "WARNING: attempting to back up a zoned block device in use! "
              "Expect data loss and corruption.\n");
      zbd = zbd_open(true, false);
    }
  }

  if (!zbd) return 1;

  std::unique_ptr<ZenFS> zenFS;
  status = zenfs_mount(zbd, &zenFS, true);
  if (!status.ok()) {
    fprintf(stderr, "Failed to mount filesystem, error: %s\n",
            status.ToString().c_str());
    return 1;
  }

  bool is_dir;
  io_status = zenFS->IsDirectory(FLAGS_backup_path, opts, &is_dir, &dbg);
  if (!io_status.ok()) {
    fprintf(stderr, "IsDirectory failed, error: %s\n",
            io_status.ToString().c_str());
    return 1;
  }

  if (!is_dir) {
    std::string dest_filename =
        FLAGS_path + "/" +
        FLAGS_backup_path.substr(FLAGS_backup_path.find_last_of('/') + 1);
    io_status =
        zenfs_tool_copy_file(zenFS.get(), FLAGS_backup_path,
                             FileSystem::Default().get(), dest_filename);
  } else {
    io_status =
        zenfs_create_directories(FileSystem::Default().get(), FLAGS_path);
    if (!io_status.ok()) {
      fprintf(stderr, "Create directory failed, error: %s\n",
              io_status.ToString().c_str());
      return 1;
    }

    std::string backup_path = FLAGS_backup_path;
    if (backup_path.size() > 0 && backup_path.back() != '/') backup_path += "/";
    io_status = zenfs_tool_copy_dir(zenFS.get(), backup_path,
                                    FileSystem::Default().get(), FLAGS_path);
  }
  if (!io_status.ok()) {
    fprintf(stderr, "Copy failed, error: %s\n", io_status.ToString().c_str());
    return 1;
  }

  wlth_map = zenFS->GetWriteLifeTimeHints();
  return SaveWriteLifeTimeHints();
}

int zenfs_tool_link() {
  Status s;
  IOStatus io_s;
  IOOptions iopts;
  IODebugContext dbg;

  if (FLAGS_src_file.empty() || FLAGS_dest_file.empty()) {
    fprintf(stderr, "Error: Specify --src_file and --dest_file to be linked\n");
    return 1;
  }
  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(false, true);
  if (!zbd) return 1;

  std::unique_ptr<ZenFS> zenFS;
  s = zenfs_mount(zbd, &zenFS, false);
  if (!s.ok()) {
    fprintf(stderr, "Failed to mount filesystem, error: %s\n",
            s.ToString().c_str());
    return 1;
  }

  io_s = zenFS->LinkFile(FLAGS_src_file, FLAGS_dest_file, iopts, &dbg);
  if (!io_s.ok()) {
    fprintf(stderr, "Link failed, error: %s\n", io_s.ToString().c_str());
    return 1;
  }
  fprintf(stdout, "Linked file %s to %s\n", FLAGS_dest_file.c_str(),
          FLAGS_src_file.c_str());

  return 0;
}

int zenfs_tool_delete_file() {
  Status s;
  IOStatus io_s;
  IOOptions iopts;
  IODebugContext dbg;

  if (FLAGS_path.empty()) {
    fprintf(stderr, "Error: Specify --path of the file to be deleted.\n");
    return 1;
  }
  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(false, true);
  if (!zbd) return 1;

  std::unique_ptr<ZenFS> zenFS;
  s = zenfs_mount(zbd, &zenFS, false);
  if (!s.ok()) {
    fprintf(stderr, "Failed to mount filesystem, error: %s\n",
            s.ToString().c_str());
    return 1;
  }

  io_s = zenFS->DeleteFile(FLAGS_path, iopts, &dbg);
  if (!io_s.ok()) {
    fprintf(stderr, "Delete failed with error: %s\n", io_s.ToString().c_str());
    return 1;
  }
  fprintf(stdout, "Deleted file %s\n", FLAGS_path.c_str());

  return 0;
}

int zenfs_tool_rename_file() {
  Status s;
  IOStatus io_s;
  IOOptions iopts;
  IODebugContext dbg;

  if (FLAGS_src_file.empty() || FLAGS_dest_file.empty()) {
    fprintf(stderr,
            "Error: Specify --src_file and --dest_file to be renamed\n");
    return 1;
  }
  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(false, true);
  if (!zbd) return 1;

  std::unique_ptr<ZenFS> zenFS;
  s = zenfs_mount(zbd, &zenFS, false);
  if (!s.ok()) {
    fprintf(stderr, "Failed to mount filesystem, error: %s\n",
            s.ToString().c_str());
    return 1;
  }

  io_s = zenFS->RenameFile(FLAGS_src_file, FLAGS_dest_file, iopts, &dbg);
  if (!io_s.ok()) {
    fprintf(stderr, "Rename failed, error: %s\n", io_s.ToString().c_str());
    return 1;
  }
  fprintf(stdout, "Renamed file %s to %s\n", FLAGS_src_file.c_str(),
          FLAGS_dest_file.c_str());

  return 0;
}

int zenfs_tool_remove_directory() {
  Status s;
  IOStatus io_s;
  IOOptions iopts;
  IODebugContext dbg;

  if (FLAGS_path.empty()) {
    fprintf(stderr, "Error: Specify --path of the directory to be deleted.\n");
    return 1;
  }
  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(false, true);
  if (!zbd) return 1;

  std::unique_ptr<ZenFS> zenFS;
  s = zenfs_mount(zbd, &zenFS, false);
  if (!s.ok()) {
    fprintf(stderr, "Failed to mount filesystem, error: %s\n",
            s.ToString().c_str());
    return 1;
  }

  if (FLAGS_force) {
    io_s = zenFS->DeleteDirRecursive(FLAGS_path, iopts, &dbg);
    if (!io_s.ok()) {
      fprintf(stderr,
              "Force delete for given directory failed with error: %s\n",
              io_s.ToString().c_str());
      return 1;
    }
    fprintf(stdout, "Force deleted directory %s\n", FLAGS_path.c_str());
  } else {
    io_s = zenFS->DeleteDir(FLAGS_path, iopts, &dbg);
    if (!io_s.ok()) {
      fprintf(stderr, "Delete for given directory failed with error: %s\n",
              io_s.ToString().c_str());
      return 1;
    }
    fprintf(stdout, "Deleted directory %s\n", FLAGS_path.c_str());
  }

  return 0;
}

int zenfs_tool_restore() {
  Status status;
  IOStatus io_status;
  IOOptions opts;
  IODebugContext dbg;
  bool is_dir;

  if (FLAGS_path.empty()) {
    fprintf(stderr, "Error: Specify --path to be restored.\n");
    return 1;
  }

  AddDirSeparatorAtEnd(FLAGS_restore_path);
  std::filesystem::path fpath(FLAGS_path);
  FLAGS_path = fpath.lexically_normal().string();
  FileSystem *f_fs = FileSystem::Default().get();
  status = f_fs->IsDirectory(FLAGS_path, opts, &is_dir, &dbg);
  if (!status.ok()) {
    fprintf(stderr, "IsDirectory failed, error: %s\n",
            status.ToString().c_str());
    return 1;
  }

  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(false, true);
  if (!zbd) return 1;

  std::unique_ptr<ZenFS> zenFS;
  status = zenfs_mount(zbd, &zenFS, false);
  if (!status.ok()) {
    fprintf(stderr, "Failed to mount filesystem, error: %s\n",
            status.ToString().c_str());
    return 1;
  }

  io_status = zenfs_create_directories(zenFS.get(), FLAGS_restore_path);
  if (!io_status.ok()) {
    fprintf(stderr, "Create directory failed, error: %s\n",
            io_status.ToString().c_str());
    return 1;
  }

  if (!is_dir) {
    std::string dest_file =
        FLAGS_restore_path + fpath.lexically_normal().filename().string();
    io_status = zenfs_tool_copy_file(f_fs, FLAGS_path, zenFS.get(), dest_file);
  } else {
    AddDirSeparatorAtEnd(FLAGS_path);
    ReadWriteLifeTimeHints();
    io_status =
        zenfs_tool_copy_dir(f_fs, FLAGS_path, zenFS.get(), FLAGS_restore_path);
  }

  if (!io_status.ok()) {
    fprintf(stderr, "Copy failed, error: %s\n", io_status.ToString().c_str());
    return 1;
  }

  return 0;
}

int zenfs_tool_dump() {
  Status s;
  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(true, false);
  if (!zbd) return 1;
  ZonedBlockDevice *zbdRaw = zbd.get();

  std::unique_ptr<ZenFS> zenFS;
  s = zenfs_mount(zbd, &zenFS, true);
  if (!s.ok()) {
    fprintf(stderr, "Failed to mount filesystem, error: %s\n",
            s.ToString().c_str());
    return 1;
  }

  std::ostream &json_stream = std::cout;
  json_stream << "{\"zones\":";
  zbdRaw->EncodeJson(json_stream);
  json_stream << ",\"files\":";
  zenFS->EncodeJson(json_stream);
  json_stream << "}";

  return 0;
}

int zenfs_tool_fsinfo() {
  Status s;
  std::unique_ptr<ZonedBlockDevice> zbd = zbd_open(true, false);
  if (!zbd) return 1;

  std::unique_ptr<ZenFS> zenFS;
  s = zenfs_mount(zbd, &zenFS, true);
  if (!s.ok()) {
    fprintf(stderr, "Failed to mount filesystem, error: %s\n",
            s.ToString().c_str());
    return 1;
  }
  std::string superblock_report;
  zenFS->ReportSuperblock(&superblock_report);
  fprintf(stdout, "%s\n", superblock_report.c_str());
  return 0;
}

}  // namespace ROCKSDB_NAMESPACE

int main(int argc, char **argv) {
  gflags::SetUsageMessage(
      std::string("\nUSAGE:\n") + argv[0] +
      +" <command> [OPTIONS]...\nCommands: mkfs, list, ls-uuid, " +
      +"df, backup, restore, dump, fs-info, link, delete, rename, rmdir");
  if (argc < 2) {
    fprintf(stderr, "You need to specify a command:\n");
    fprintf(stderr,
            "\t./zenfs [list | ls-uuid | df | backup | restore | dump | "
            "fs-info | link | delete | rename | rmdir]\n");
    return 1;
  }

  gflags::SetVersionString(ZENFS_VERSION);
  std::string subcmd(argv[1]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  if (FLAGS_zbd.empty() && subcmd != "ls-uuid") {
    fprintf(stderr, "You need to specify a zoned block device using --zbd\n");
    return 1;
  }
  if (subcmd == "mkfs") {
    return ROCKSDB_NAMESPACE::zenfs_tool_mkfs();
  } else if (subcmd == "list") {
    return ROCKSDB_NAMESPACE::zenfs_tool_list();
  } else if (subcmd == "ls-uuid") {
    return ROCKSDB_NAMESPACE::zenfs_tool_lsuuid();
  } else if (subcmd == "df") {
    return ROCKSDB_NAMESPACE::zenfs_tool_df();
  } else if (subcmd == "backup") {
    return ROCKSDB_NAMESPACE::zenfs_tool_backup();
  } else if (subcmd == "restore") {
    return ROCKSDB_NAMESPACE::zenfs_tool_restore();
  } else if (subcmd == "dump") {
    return ROCKSDB_NAMESPACE::zenfs_tool_dump();
  } else if (subcmd == "fs-info") {
    return ROCKSDB_NAMESPACE::zenfs_tool_fsinfo();
  } else if (subcmd == "link") {
    return ROCKSDB_NAMESPACE::zenfs_tool_link();
  } else if (subcmd == "delete") {
    return ROCKSDB_NAMESPACE::zenfs_tool_delete_file();
  } else if (subcmd == "rename") {
    return ROCKSDB_NAMESPACE::zenfs_tool_rename_file();
  } else if (subcmd == "rmdir") {
    return ROCKSDB_NAMESPACE::zenfs_tool_remove_directory();
  } else {
    fprintf(stderr, "Subcommand not recognized: %s\n", subcmd.c_str());
    return 1;
  }

  return 0;
}
