// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#if !defined(ROCKSDB_LITE) && defined(OS_LINUX)

#include "fs_zenfs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sstream>
#include <utility>
#include <vector>

#include "rocksdb/utilities/object_registry.h"
#include "snapshot.h"
#include "util/coding.h"
#include "util/crc32c.h"

#define DEFAULT_ZENV_LOG_PATH "/tmp/"

namespace ROCKSDB_NAMESPACE {

Status Superblock::DecodeFrom(Slice* input) {
  if (input->size() != ENCODED_SIZE) {
    return Status::Corruption("ZenFS Superblock",
                              "Error: Superblock size missmatch");
  }

  GetFixed32(input, &magic_);
  memcpy(&uuid_, input->data(), sizeof(uuid_));
  input->remove_prefix(sizeof(uuid_));
  GetFixed32(input, &sequence_);
  GetFixed32(input, &superblock_version_);
  GetFixed32(input, &flags_);
  GetFixed32(input, &block_size_);
  GetFixed32(input, &zone_size_);
  GetFixed32(input, &nr_zones_);
  GetFixed32(input, &finish_treshold_);
  memcpy(&aux_fs_path_, input->data(), sizeof(aux_fs_path_));
  input->remove_prefix(sizeof(aux_fs_path_));
  memcpy(&zenfs_version_, input->data(), sizeof(zenfs_version_));
  input->remove_prefix(sizeof(zenfs_version_));
  memcpy(&reserved_, input->data(), sizeof(reserved_));
  input->remove_prefix(sizeof(reserved_));
  assert(input->size() == 0);

  if (magic_ != MAGIC)
    return Status::Corruption("ZenFS Superblock", "Error: Magic missmatch");
  if (superblock_version_ != CURRENT_SUPERBLOCK_VERSION) {
    return Status::Corruption(
        "ZenFS Superblock",
        "Error: Incompatible ZenFS on-disk format version, "
        "please migrate data or switch to previously used ZenFS version. "
        "See the ZenFS README for instructions.");
  }

  return Status::OK();
}

void Superblock::EncodeTo(std::string* output) {
  sequence_++; /* Ensure that this superblock representation is unique */
  output->clear();
  PutFixed32(output, magic_);
  output->append(uuid_, sizeof(uuid_));
  PutFixed32(output, sequence_);
  PutFixed32(output, superblock_version_);
  PutFixed32(output, flags_);
  PutFixed32(output, block_size_);
  PutFixed32(output, zone_size_);
  PutFixed32(output, nr_zones_);
  PutFixed32(output, finish_treshold_);
  output->append(aux_fs_path_, sizeof(aux_fs_path_));
  output->append(zenfs_version_, sizeof(zenfs_version_));
  output->append(reserved_, sizeof(reserved_));
  assert(output->length() == ENCODED_SIZE);
}

void Superblock::GetReport(std::string* reportString) {
  reportString->append("Magic:\t\t\t\t");
  PutFixed32(reportString, magic_);
  reportString->append("\nUUID:\t\t\t\t");
  reportString->append(uuid_);
  reportString->append("\nSequence Number:\t\t");
  reportString->append(std::to_string(sequence_));
  reportString->append("\nSuperblock Version:\t\t");
  reportString->append(std::to_string(superblock_version_));
  reportString->append("\nFlags [Decimal]:\t\t");
  reportString->append(std::to_string(flags_));
  reportString->append("\nBlock Size [Bytes]:\t\t");
  reportString->append(std::to_string(block_size_));
  reportString->append("\nZone Size [Blocks]:\t\t");
  reportString->append(std::to_string(zone_size_));
  reportString->append("\nNumber of Zones:\t\t");
  reportString->append(std::to_string(nr_zones_));
  reportString->append("\nFinish Threshold [%]:\t\t");
  reportString->append(std::to_string(finish_treshold_));
  reportString->append("\nAuxiliary FS Path:\t\t");
  reportString->append(aux_fs_path_);
  reportString->append("\nZenFS Version:\t\t\t");
  std::string zenfs_version = zenfs_version_;
  if (zenfs_version.length() == 0) {
    zenfs_version = "Not Available";
  }
  reportString->append(zenfs_version);
}

Status Superblock::CompatibleWith(ZonedBlockDevice* zbd) {
  if (block_size_ != zbd->GetBlockSize())
    return Status::Corruption("ZenFS Superblock",
                              "Error: block size missmatch");
  if (zone_size_ != (zbd->GetZoneSize() / block_size_))
    return Status::Corruption("ZenFS Superblock", "Error: zone size missmatch");
  if (nr_zones_ > zbd->GetNrZones())
    return Status::Corruption("ZenFS Superblock",
                              "Error: nr of zones missmatch");

  return Status::OK();
}

IOStatus ZenMetaLog::AddRecord(const Slice& slice) {
  uint32_t record_sz = slice.size();
  const char* data = slice.data();
  size_t phys_sz;
  uint32_t crc = 0;
  char* buffer;
  int ret;
  IOStatus s;

  phys_sz = record_sz + zMetaHeaderSize;

  if (phys_sz % bs_) phys_sz += bs_ - phys_sz % bs_;

  assert(data != nullptr);
  assert((phys_sz % bs_) == 0);

  ret = posix_memalign((void**)&buffer, sysconf(_SC_PAGESIZE), phys_sz);
  if (ret) return IOStatus::IOError("Failed to allocate memory");

  memset(buffer, 0, phys_sz);

  crc = crc32c::Extend(crc, (const char*)&record_sz, sizeof(uint32_t));
  crc = crc32c::Extend(crc, data, record_sz);
  crc = crc32c::Mask(crc);

  EncodeFixed32(buffer, crc);
  EncodeFixed32(buffer + sizeof(uint32_t), record_sz);
  memcpy(buffer + sizeof(uint32_t) * 2, data, record_sz);

  s = zone_->Append(buffer, phys_sz);

  free(buffer);
  return s;
}

IOStatus ZenMetaLog::Read(Slice* slice) {
  int f = zbd_->GetReadFD();
  const char* data = slice->data();
  size_t read = 0;
  size_t to_read = slice->size();
  int ret;

  if (read_pos_ >= zone_->wp_) {
    // EOF
    slice->clear();
    return IOStatus::OK();
  }

  if ((read_pos_ + to_read) > (zone_->start_ + zone_->max_capacity_)) {
    return IOStatus::IOError("Read across zone");
  }

  while (read < to_read) {
    ret = pread(f, (void*)(data + read), to_read - read, read_pos_);

    if (ret == -1 && errno == EINTR) continue;
    if (ret < 0) return IOStatus::IOError("Read failed");

    read += ret;
    read_pos_ += ret;
  }

  return IOStatus::OK();
}

IOStatus ZenMetaLog::ReadRecord(Slice* record, std::string* scratch) {
  Slice header;
  uint32_t record_sz = 0;
  uint32_t record_crc = 0;
  uint32_t actual_crc;
  IOStatus s;

  scratch->clear();
  record->clear();

  scratch->append(zMetaHeaderSize, 0);
  header = Slice(scratch->c_str(), zMetaHeaderSize);

  s = Read(&header);
  if (!s.ok()) return s;

  // EOF?
  if (header.size() == 0) {
    record->clear();
    return IOStatus::OK();
  }

  GetFixed32(&header, &record_crc);
  GetFixed32(&header, &record_sz);

  scratch->clear();
  scratch->append(record_sz, 0);

  *record = Slice(scratch->c_str(), record_sz);
  s = Read(record);
  if (!s.ok()) return s;

  actual_crc = crc32c::Value((const char*)&record_sz, sizeof(uint32_t));
  actual_crc = crc32c::Extend(actual_crc, record->data(), record->size());

  if (actual_crc != crc32c::Unmask(record_crc)) {
    return IOStatus::IOError("Not a valid record");
  }

  /* Next record starts on a block boundary */
  if (read_pos_ % bs_) read_pos_ += bs_ - (read_pos_ % bs_);

  return IOStatus::OK();
}

ZenFS::ZenFS(ZonedBlockDevice* zbd, std::shared_ptr<FileSystem> aux_fs,
             std::shared_ptr<Logger> logger)
    : FileSystemWrapper(aux_fs), zbd_(zbd), logger_(logger) {
  Info(logger_, "ZenFS initializing");
  Info(logger_, "ZenFS parameters: block device: %s, aux filesystem: %s",
       zbd_->GetFilename().c_str(), target()->Name());

  Info(logger_, "ZenFS initializing");
  next_file_id_ = 1;
  metadata_writer_.zenFS = this;
}

ZenFS::~ZenFS() {
  Status s;
  Info(logger_, "ZenFS shutting down");
  zbd_->LogZoneUsage();
  LogFiles();

  meta_log_.reset(nullptr);
  ClearFiles();
  delete zbd_;
}

IOStatus ZenFS::Repair() {
  std::map<std::string, std::shared_ptr<ZoneFile>>::iterator it;
  for (it = files_.begin(); it != files_.end(); it++) {
    std::shared_ptr<ZoneFile> zFile = it->second;
    if (zFile->HasActiveExtent()) {
      IOStatus s = zFile->Recover();
      if (!s.ok()) return s;
    }
  }

  return IOStatus::OK();
}

std::string ZenFS::FormatPathLexically(fs::path filepath) {
  fs::path ret = fs::path("/") / filepath.lexically_normal();
  return ret.string();
}

void ZenFS::LogFiles() {
  std::map<std::string, std::shared_ptr<ZoneFile>>::iterator it;
  uint64_t total_size = 0;

  Info(logger_, "  Files:\n");
  for (it = files_.begin(); it != files_.end(); it++) {
    std::shared_ptr<ZoneFile> zFile = it->second;
    std::vector<ZoneExtent*> extents = zFile->GetExtents();

    Info(logger_, "    %-45s sz: %lu lh: %d sparse: %u", it->first.c_str(),
         zFile->GetFileSize(), zFile->GetWriteLifeTimeHint(),
         zFile->IsSparse());
    for (unsigned int i = 0; i < extents.size(); i++) {
      ZoneExtent* extent = extents[i];
      Info(logger_, "          Extent %u {start=0x%lx, zone=%u, len=%lu} ", i,
           extent->start_,
           (uint32_t)(extent->zone_->start_ / zbd_->GetZoneSize()),
           extent->length_);

      total_size += extent->length_;
    }
  }
  Info(logger_, "Sum of all files: %lu MB of data \n",
       total_size / (1024 * 1024));
}

void ZenFS::ClearFiles() {
  std::map<std::string, std::shared_ptr<ZoneFile>>::iterator it;
  std::lock_guard<std::mutex> file_lock(files_mtx_);
  for (it = files_.begin(); it != files_.end(); it++) it->second.reset();
  files_.clear();
}

/* Assumes that files_mutex_ is held */
IOStatus ZenFS::WriteSnapshotLocked(ZenMetaLog* meta_log) {
  IOStatus s;
  std::string snapshot;

  EncodeSnapshotTo(&snapshot);
  s = meta_log->AddRecord(snapshot);
  if (s.ok()) {
    for (auto it = files_.begin(); it != files_.end(); it++) {
      std::shared_ptr<ZoneFile> zoneFile = it->second;
      zoneFile->MetadataSynced();
    }
  }
  return s;
}

IOStatus ZenFS::WriteEndRecord(ZenMetaLog* meta_log) {
  std::string endRecord;

  PutFixed32(&endRecord, kEndRecord);
  return meta_log->AddRecord(endRecord);
}

/* Assumes the files_mtx_ is held */
IOStatus ZenFS::RollMetaZoneLocked() {
  std::unique_ptr<ZenMetaLog> new_meta_log, old_meta_log;
  Zone* new_meta_zone = nullptr;
  IOStatus s;

  ZenFSMetricsLatencyGuard guard(zbd_->GetMetrics(), ZENFS_ROLL_LATENCY,
                                 Env::Default());
  zbd_->GetMetrics()->ReportQPS(ZENFS_ROLL_QPS, 1);

  IOStatus status = zbd_->AllocateMetaZone(&new_meta_zone);
  if (!status.ok()) return status;

  if (!new_meta_zone) {
    assert(false);  // TMP
    Error(logger_, "Out of metadata zones, we should go to read only now.");
    return IOStatus::NoSpace("Out of metadata zones");
  }

  Info(logger_, "Rolling to metazone %d\n", (int)new_meta_zone->GetZoneNr());
  new_meta_log.reset(new ZenMetaLog(zbd_, new_meta_zone));

  old_meta_log.swap(meta_log_);
  meta_log_.swap(new_meta_log);

  /* Write an end record and finish the meta data zone if there is space left */
  if (old_meta_log->GetZone()->GetCapacityLeft())
    WriteEndRecord(old_meta_log.get());
  if (old_meta_log->GetZone()->GetCapacityLeft())
    old_meta_log->GetZone()->Finish();

  std::string super_string;
  superblock_->EncodeTo(&super_string);

  s = meta_log_->AddRecord(super_string);
  if (!s.ok()) {
    Error(logger_,
          "Could not write super block when rolling to a new meta zone");
    return IOStatus::IOError("Failed writing a new superblock");
  }

  s = WriteSnapshotLocked(meta_log_.get());

  /* We've rolled successfully, we can reset the old zone now */
  if (s.ok()) old_meta_log->GetZone()->Reset();

  return s;
}

IOStatus ZenFS::PersistSnapshot(ZenMetaLog* meta_writer) {
  IOStatus s;

  std::lock_guard<std::mutex> file_lock(files_mtx_);
  std::lock_guard<std::mutex> metadata_lock(metadata_sync_mtx_);

  s = WriteSnapshotLocked(meta_writer);
  if (s == IOStatus::NoSpace()) {
    Info(logger_, "Current meta zone full, rolling to next meta zone");
    s = RollMetaZoneLocked();
  }

  if (!s.ok()) {
    Error(logger_,
          "Failed persisting a snapshot, we should go to read only now!");
  }

  return s;
}

IOStatus ZenFS::PersistRecord(std::string record) {
  IOStatus s;

  std::lock_guard<std::mutex> lock(metadata_sync_mtx_);
  s = meta_log_->AddRecord(record);
  if (s == IOStatus::NoSpace()) {
    Info(logger_, "Current meta zone full, rolling to next meta zone");
    s = RollMetaZoneLocked();
    /* After a successfull roll, a complete snapshot has been persisted
     * - no need to write the record update */
  }

  return s;
}

IOStatus ZenFS::SyncFileExtents(ZoneFile* zoneFile,
                                std::vector<ZoneExtent*> new_extents) {
  IOStatus s;

  std::vector<ZoneExtent*> old_extents = zoneFile->GetExtents();
  zoneFile->ReplaceExtentList(new_extents);
  zoneFile->MetadataUnsynced();
  s = SyncFileMetadata(zoneFile, true);

  if (!s.ok()) {
    return s;
  }

  // Clear changed extents' zone stats
  for (size_t i = 0; i < new_extents.size(); ++i) {
    ZoneExtent* old_ext = old_extents[i];
    if (old_ext->start_ != new_extents[i]->start_) {
      old_ext->zone_->used_capacity_ -= old_ext->length_;
    }
    delete old_ext;
  }

  return IOStatus::OK();
}

/* Must hold files_mtx_ */
IOStatus ZenFS::SyncFileMetadataNoLock(ZoneFile* zoneFile, bool replace) {
  std::string fileRecord;
  std::string output;
  IOStatus s;
  ZenFSMetricsLatencyGuard guard(zbd_->GetMetrics(), ZENFS_META_SYNC_LATENCY,
                                 Env::Default());

  if (zoneFile->IsDeleted()) {
    Info(logger_, "File %s has been deleted, skip sync file metadata!",
         zoneFile->GetFilename().c_str());
    return IOStatus::OK();
  }

  if (replace) {
    PutFixed32(&output, kFileReplace);
  } else {
    zoneFile->SetFileModificationTime(time(0));
    PutFixed32(&output, kFileUpdate);
  }
  zoneFile->EncodeUpdateTo(&fileRecord);
  PutLengthPrefixedSlice(&output, Slice(fileRecord));

  s = PersistRecord(output);
  if (s.ok()) zoneFile->MetadataSynced();

  return s;
}

IOStatus ZenFS::SyncFileMetadata(ZoneFile* zoneFile, bool replace) {
  std::lock_guard<std::mutex> lock(files_mtx_);
  return SyncFileMetadataNoLock(zoneFile, replace);
}

/* Must hold files_mtx_ */
std::shared_ptr<ZoneFile> ZenFS::GetFileNoLock(std::string fname) {
  std::shared_ptr<ZoneFile> zoneFile(nullptr);
  fname = FormatPathLexically(fname);
  if (files_.find(fname) != files_.end()) {
    zoneFile = files_[fname];
  }
  return zoneFile;
}

std::shared_ptr<ZoneFile> ZenFS::GetFile(std::string fname) {
  std::shared_ptr<ZoneFile> zoneFile(nullptr);
  std::lock_guard<std::mutex> lock(files_mtx_);
  zoneFile = GetFileNoLock(fname);
  return zoneFile;
}

/* Must hold files_mtx_ */
IOStatus ZenFS::DeleteFileNoLock(std::string fname, const IOOptions& options,
                                 IODebugContext* dbg) {
  std::shared_ptr<ZoneFile> zoneFile(nullptr);
  IOStatus s;

  fname = FormatPathLexically(fname);
  zoneFile = GetFileNoLock(fname);
  if (zoneFile != nullptr) {
    std::string record;

    files_.erase(fname);
    s = zoneFile->RemoveLinkName(fname);
    if (!s.ok()) return s;
    EncodeFileDeletionTo(zoneFile, &record, fname);
    s = PersistRecord(record);
    if (!s.ok()) {
      /* Failed to persist the delete, return to a consistent state */
      files_.insert(std::make_pair(fname.c_str(), zoneFile));
      zoneFile->AddLinkName(fname);
    } else {
      if (zoneFile->GetNrLinks() > 0) return s;
      /* Mark up the file as deleted so it won't be migrated by GC */
      zoneFile->SetDeleted();
      zoneFile.reset();
    }
  } else {
    s = target()->DeleteFile(ToAuxPath(fname), options, dbg);
  }

  return s;
}

IOStatus ZenFS::NewSequentialFile(const std::string& filename,
                                  const FileOptions& file_opts,
                                  std::unique_ptr<FSSequentialFile>* result,
                                  IODebugContext* dbg) {
  std::string fname = FormatPathLexically(filename);
  std::shared_ptr<ZoneFile> zoneFile = GetFile(fname);

  Debug(logger_, "New sequential file: %s direct: %d\n", fname.c_str(),
        file_opts.use_direct_reads);

  if (zoneFile == nullptr) {
    return target()->NewSequentialFile(ToAuxPath(fname), file_opts, result,
                                       dbg);
  }

  result->reset(new ZonedSequentialFile(zoneFile, file_opts));
  return IOStatus::OK();
}

IOStatus ZenFS::NewRandomAccessFile(const std::string& filename,
                                    const FileOptions& file_opts,
                                    std::unique_ptr<FSRandomAccessFile>* result,
                                    IODebugContext* dbg) {
  std::string fname = FormatPathLexically(filename);
  std::shared_ptr<ZoneFile> zoneFile = GetFile(fname);

  Debug(logger_, "New random access file: %s direct: %d\n", fname.c_str(),
        file_opts.use_direct_reads);

  if (zoneFile == nullptr) {
    return target()->NewRandomAccessFile(ToAuxPath(fname), file_opts, result,
                                         dbg);
  }

  result->reset(new ZonedRandomAccessFile(files_[fname], file_opts));
  return IOStatus::OK();
}

inline bool ends_with(std::string const& value, std::string const& ending) {
  if (ending.size() > value.size()) return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

IOStatus ZenFS::NewWritableFile(const std::string& filename,
                                const FileOptions& file_opts,
                                std::unique_ptr<FSWritableFile>* result,
                                IODebugContext* /*dbg*/) {
  std::string fname = FormatPathLexically(filename);
  Debug(logger_, "New writable file: %s direct: %d\n", fname.c_str(),
        file_opts.use_direct_writes);

  return OpenWritableFile(fname, file_opts, result, nullptr, false);
}

IOStatus ZenFS::ReuseWritableFile(const std::string& filename,
                                  const std::string& old_filename,
                                  const FileOptions& file_opts,
                                  std::unique_ptr<FSWritableFile>* result,
                                  IODebugContext* dbg) {
  IOStatus s;
  std::string fname = FormatPathLexically(filename);
  std::string old_fname = FormatPathLexically(old_filename);
  Debug(logger_, "Reuse writable file: %s old name: %s\n", fname.c_str(),
        old_fname.c_str());

  if (GetFile(old_fname) == nullptr)
    return IOStatus::NotFound("Old file does not exist");

  /*
   * Delete the old file as it cannot be written from start of file
   * and create a new file with fname
   */
  s = DeleteFile(old_fname, file_opts.io_options, dbg);
  if (!s.ok()) {
    Error(logger_, "Failed to delete file %s\n", old_fname.c_str());
    return s;
  }

  return OpenWritableFile(fname, file_opts, result, dbg, false);
}

IOStatus ZenFS::FileExists(const std::string& filename,
                           const IOOptions& options, IODebugContext* dbg) {
  std::string fname = FormatPathLexically(filename);
  Debug(logger_, "FileExists: %s \n", fname.c_str());

  if (GetFile(fname) == nullptr) {
    return target()->FileExists(ToAuxPath(fname), options, dbg);
  } else {
    return IOStatus::OK();
  }
}

/* If the file does not exist, create a new one,
 * else return the existing file
 */
IOStatus ZenFS::ReopenWritableFile(const std::string& filename,
                                   const FileOptions& file_opts,
                                   std::unique_ptr<FSWritableFile>* result,
                                   IODebugContext* dbg) {
  std::string fname = FormatPathLexically(filename);
  Debug(logger_, "Reopen writable file: %s \n", fname.c_str());

  return OpenWritableFile(fname, file_opts, result, dbg, true);
}

/* Must hold files_mtx_ */
void ZenFS::GetZenFSChildrenNoLock(const std::string& dir,
                                   bool include_grandchildren,
                                   std::vector<std::string>* result) {
  auto path_as_string_with_separator_at_end = [](fs::path const& path) {
    fs::path with_sep = path / fs::path("");
    return with_sep.lexically_normal().string();
  };

  auto string_starts_with = [](std::string const& string,
                               std::string const& needle) {
    return string.rfind(needle, 0) == 0;
  };

  std::string dir_with_terminating_seperator =
      path_as_string_with_separator_at_end(fs::path(dir));

  auto relative_child_path =
      [&dir_with_terminating_seperator](std::string const& full_path) {
        return full_path.substr(dir_with_terminating_seperator.length());
      };

  for (auto const& it : files_) {
    fs::path file_path(it.first);
    assert(file_path.has_filename());

    std::string file_dir =
        path_as_string_with_separator_at_end(file_path.parent_path());

    if (string_starts_with(file_dir, dir_with_terminating_seperator)) {
      if (include_grandchildren ||
          file_dir.length() == dir_with_terminating_seperator.length()) {
        result->push_back(relative_child_path(file_path.string()));
      }
    }
  }
}

/* Must hold files_mtx_ */
IOStatus ZenFS::GetChildrenNoLock(const std::string& dir_path,
                                  const IOOptions& options,
                                  std::vector<std::string>* result,
                                  IODebugContext* dbg) {
  std::vector<std::string> auxfiles;
  std::string dir = FormatPathLexically(dir_path);
  IOStatus s;

  Debug(logger_, "GetChildrenNoLock: %s \n", dir.c_str());

  s = target()->GetChildren(ToAuxPath(dir), options, &auxfiles, dbg);
  if (!s.ok()) {
    /* On ZenFS empty directories cannot be created, therefore we cannot
       distinguish between "Directory not found" and "Directory is empty"
       and always return empty lists with OK status in both cases. */
    if (s.IsNotFound()) {
      return IOStatus::OK();
    }
    return s;
  }

  for (const auto& f : auxfiles) {
    if (f != "." && f != "..") result->push_back(f);
  }

  GetZenFSChildrenNoLock(dir, false, result);

  return s;
}

IOStatus ZenFS::GetChildren(const std::string& dir, const IOOptions& options,
                            std::vector<std::string>* result,
                            IODebugContext* dbg) {
  std::lock_guard<std::mutex> lock(files_mtx_);
  return GetChildrenNoLock(dir, options, result, dbg);
}

/* Must hold files_mtx_ */
IOStatus ZenFS::DeleteDirRecursiveNoLock(const std::string& dir,
                                         const IOOptions& options,
                                         IODebugContext* dbg) {
  std::vector<std::string> children;
  std::string d = FormatPathLexically(dir);
  IOStatus s;

  Debug(logger_, "DeleteDirRecursiveNoLock: %s aux: %s\n", d.c_str(),
        ToAuxPath(d).c_str());

  s = GetChildrenNoLock(d, options, &children, dbg);
  if (!s.ok()) {
    return s;
  }

  for (const auto& child : children) {
    std::string file_to_delete = (fs::path(d) / fs::path(child)).string();
    bool is_dir;

    s = IsDirectoryNoLock(file_to_delete, options, &is_dir, dbg);
    if (!s.ok()) {
      return s;
    }

    if (is_dir) {
      s = DeleteDirRecursiveNoLock(file_to_delete, options, dbg);
    } else {
      s = DeleteFileNoLock(file_to_delete, options, dbg);
    }
    if (!s.ok()) {
      return s;
    }
  }

  return target()->DeleteDir(ToAuxPath(d), options, dbg);
}

IOStatus ZenFS::DeleteDirRecursive(const std::string& d,
                                   const IOOptions& options,
                                   IODebugContext* dbg) {
  IOStatus s;
  {
    std::lock_guard<std::mutex> lock(files_mtx_);
    s = DeleteDirRecursiveNoLock(d, options, dbg);
  }
  if (s.ok()) s = zbd_->ResetUnusedIOZones();
  return s;
}

IOStatus ZenFS::OpenWritableFile(const std::string& filename,
                                 const FileOptions& file_opts,
                                 std::unique_ptr<FSWritableFile>* result,
                                 IODebugContext* dbg, bool reopen) {
  IOStatus s;
  std::string fname = FormatPathLexically(filename);
  bool resetIOZones = false;
  {
    std::lock_guard<std::mutex> file_lock(files_mtx_);
    std::shared_ptr<ZoneFile> zoneFile = GetFileNoLock(fname);

    /* if reopen is true and the file exists, return it */
    if (reopen && zoneFile != nullptr) {
      zoneFile->AcquireWRLock();
      result->reset(
          new ZonedWritableFile(zbd_, !file_opts.use_direct_writes, zoneFile));
      return IOStatus::OK();
    }

    if (zoneFile != nullptr) {
      s = DeleteFileNoLock(fname, file_opts.io_options, dbg);
      if (!s.ok()) return s;
      resetIOZones = true;
    }

    zoneFile =
        std::make_shared<ZoneFile>(zbd_, next_file_id_++, &metadata_writer_);
    zoneFile->SetFileModificationTime(time(0));
    zoneFile->AddLinkName(fname);

    /* RocksDB does not set the right io type(!)*/
    if (ends_with(fname, ".log")) {
      zoneFile->SetIOType(IOType::kWAL);
      zoneFile->SetSparse(!file_opts.use_direct_writes);
    } else {
      zoneFile->SetIOType(IOType::kUnknown);
    }

    /* Persist the creation of the file */
    s = SyncFileMetadataNoLock(zoneFile);
    if (!s.ok()) {
      zoneFile.reset();
      return s;
    }

    zoneFile->AcquireWRLock();
    files_.insert(std::make_pair(fname.c_str(), zoneFile));
    result->reset(
        new ZonedWritableFile(zbd_, !file_opts.use_direct_writes, zoneFile));
  }

  if (resetIOZones) s = zbd_->ResetUnusedIOZones();

  return s;
}

IOStatus ZenFS::DeleteFile(const std::string& fname, const IOOptions& options,
                           IODebugContext* dbg) {
  IOStatus s;

  Debug(logger_, "DeleteFile: %s \n", fname.c_str());

  files_mtx_.lock();
  s = DeleteFileNoLock(fname, options, dbg);
  files_mtx_.unlock();
  if (s.ok()) s = zbd_->ResetUnusedIOZones();
  zbd_->LogZoneStats();

  return s;
}

IOStatus ZenFS::GetFileModificationTime(const std::string& filename,
                                        const IOOptions& options,
                                        uint64_t* mtime, IODebugContext* dbg) {
  std::shared_ptr<ZoneFile> zoneFile(nullptr);
  std::string f = FormatPathLexically(filename);
  IOStatus s;

  Debug(logger_, "GetFileModificationTime: %s \n", f.c_str());
  std::lock_guard<std::mutex> lock(files_mtx_);
  if (files_.find(f) != files_.end()) {
    zoneFile = files_[f];
    *mtime = (uint64_t)zoneFile->GetFileModificationTime();
  } else {
    s = target()->GetFileModificationTime(ToAuxPath(f), options, mtime, dbg);
  }
  return s;
}

IOStatus ZenFS::GetFileSize(const std::string& filename,
                            const IOOptions& options, uint64_t* size,
                            IODebugContext* dbg) {
  std::shared_ptr<ZoneFile> zoneFile(nullptr);
  std::string f = FormatPathLexically(filename);
  IOStatus s;

  Debug(logger_, "GetFileSize: %s \n", f.c_str());

  std::lock_guard<std::mutex> lock(files_mtx_);
  if (files_.find(f) != files_.end()) {
    zoneFile = files_[f];
    *size = zoneFile->GetFileSize();
  } else {
    s = target()->GetFileSize(ToAuxPath(f), options, size, dbg);
  }

  return s;
}

/* Must hold files_mtx_ */
IOStatus ZenFS::RenameChildNoLock(std::string const& source_dir,
                                  std::string const& dest_dir,
                                  std::string const& child,
                                  const IOOptions& options,
                                  IODebugContext* dbg) {
  std::string source_child = (fs::path(source_dir) / fs::path(child)).string();
  std::string dest_child = (fs::path(dest_dir) / fs::path(child)).string();
  return RenameFileNoLock(source_child, dest_child, options, dbg);
}

/* Must hold files_mtx_ */
IOStatus ZenFS::RollbackAuxDirRenameNoLock(
    const std::string& source_path, const std::string& dest_path,
    const std::vector<std::string>& renamed_children, const IOOptions& options,
    IODebugContext* dbg) {
  IOStatus s;

  for (const auto& rollback_child : renamed_children) {
    s = RenameChildNoLock(dest_path, source_path, rollback_child, options, dbg);
    if (!s.ok()) {
      return IOStatus::Corruption(
          "RollbackAuxDirRenameNoLock: Failed to roll back directory rename");
    }
  }

  s = target()->RenameFile(ToAuxPath(dest_path), ToAuxPath(source_path),
                           options, dbg);
  if (!s.ok()) {
    return IOStatus::Corruption(
        "RollbackAuxDirRenameNoLock: Failed to roll back auxiliary path "
        "renaming");
  }

  return s;
}

/* Must hold files_mtx_ */
IOStatus ZenFS::RenameAuxPathNoLock(const std::string& source_path,
                                    const std::string& dest_path,
                                    const IOOptions& options,
                                    IODebugContext* dbg) {
  IOStatus s;
  std::vector<std::string> children;
  std::vector<std::string> renamed_children;

  s = target()->RenameFile(ToAuxPath(source_path), ToAuxPath(dest_path),
                           options, dbg);
  if (!s.ok()) {
    return s;
  }

  GetZenFSChildrenNoLock(source_path, true, &children);

  for (const auto& child : children) {
    s = RenameChildNoLock(source_path, dest_path, child, options, dbg);
    if (!s.ok()) {
      IOStatus failed_rename = s;
      s = RollbackAuxDirRenameNoLock(source_path, dest_path, renamed_children,
                                     options, dbg);
      if (!s.ok()) {
        return s;
      }
      return failed_rename;
    }
    renamed_children.push_back(child);
  }

  return s;
}

/* Must hold files_mtx_ */
IOStatus ZenFS::RenameFileNoLock(const std::string& src_path,
                                 const std::string& dst_path,
                                 const IOOptions& options,
                                 IODebugContext* dbg) {
  std::shared_ptr<ZoneFile> source_file(nullptr);
  std::shared_ptr<ZoneFile> existing_dest_file(nullptr);
  std::string source_path = FormatPathLexically(src_path);
  std::string dest_path = FormatPathLexically(dst_path);
  IOStatus s;

  Debug(logger_, "Rename file: %s to : %s\n", source_path.c_str(),
        dest_path.c_str());

  source_file = GetFileNoLock(source_path);
  if (source_file != nullptr) {
    existing_dest_file = GetFileNoLock(dest_path);
    if (existing_dest_file != nullptr) {
      s = DeleteFileNoLock(dest_path, options, dbg);
      if (!s.ok()) {
        return s;
      }
    }

    s = source_file->RenameLink(source_path, dest_path);
    if (!s.ok()) return s;
    files_.erase(source_path);

    files_.insert(std::make_pair(dest_path, source_file));

    s = SyncFileMetadataNoLock(source_file);
    if (!s.ok()) {
      /* Failed to persist the rename, roll back */
      files_.erase(dest_path);
      s = source_file->RenameLink(dest_path, source_path);
      if (!s.ok()) return s;
      files_.insert(std::make_pair(source_path, source_file));
    }
  } else {
    s = RenameAuxPathNoLock(source_path, dest_path, options, dbg);
  }

  return s;
}

IOStatus ZenFS::RenameFile(const std::string& source_path,
                           const std::string& dest_path,
                           const IOOptions& options, IODebugContext* dbg) {
  IOStatus s;
  {
    std::lock_guard<std::mutex> lock(files_mtx_);
    s = RenameFileNoLock(source_path, dest_path, options, dbg);
  }
  if (s.ok()) s = zbd_->ResetUnusedIOZones();
  return s;
}

IOStatus ZenFS::LinkFile(const std::string& file, const std::string& link,
                         const IOOptions& options, IODebugContext* dbg) {
  std::shared_ptr<ZoneFile> src_file(nullptr);
  std::string fname = FormatPathLexically(file);
  std::string lname = FormatPathLexically(link);
  IOStatus s;

  Debug(logger_, "LinkFile: %s to %s\n", fname.c_str(), lname.c_str());
  {
    std::lock_guard<std::mutex> lock(files_mtx_);

    if (GetFileNoLock(lname) != nullptr)
      return IOStatus::InvalidArgument("Failed to create link, target exists");

    src_file = GetFileNoLock(fname);
    if (src_file != nullptr) {
      src_file->AddLinkName(lname);
      files_.insert(std::make_pair(lname, src_file));
      s = SyncFileMetadataNoLock(src_file);
      if (!s.ok()) {
        s = src_file->RemoveLinkName(lname);
        if (!s.ok()) return s;
        files_.erase(lname);
      }
      return s;
    }
  }
  s = target()->LinkFile(ToAuxPath(fname), ToAuxPath(lname), options, dbg);
  return s;
}

IOStatus ZenFS::NumFileLinks(const std::string& file, const IOOptions& options,
                             uint64_t* nr_links, IODebugContext* dbg) {
  std::shared_ptr<ZoneFile> src_file(nullptr);
  std::string fname = FormatPathLexically(file);
  IOStatus s;

  Debug(logger_, "NumFileLinks: %s\n", fname.c_str());
  {
    std::lock_guard<std::mutex> lock(files_mtx_);

    src_file = GetFileNoLock(fname);
    if (src_file != nullptr) {
      *nr_links = (uint64_t)src_file->GetNrLinks();
      return IOStatus::OK();
    }
  }
  s = target()->NumFileLinks(ToAuxPath(fname), options, nr_links, dbg);
  return s;
}

IOStatus ZenFS::AreFilesSame(const std::string& file, const std::string& linkf,
                             const IOOptions& options, bool* res,
                             IODebugContext* dbg) {
  std::shared_ptr<ZoneFile> src_file(nullptr);
  std::shared_ptr<ZoneFile> dst_file(nullptr);
  std::string fname = FormatPathLexically(file);
  std::string link = FormatPathLexically(linkf);
  IOStatus s;

  Debug(logger_, "AreFilesSame: %s, %s\n", fname.c_str(), link.c_str());

  {
    std::lock_guard<std::mutex> lock(files_mtx_);
    src_file = GetFileNoLock(fname);
    dst_file = GetFileNoLock(link);
    if (src_file != nullptr && dst_file != nullptr) {
      if (src_file->GetID() == dst_file->GetID())
        *res = true;
      else
        *res = false;
      return IOStatus::OK();
    }
  }
  s = target()->AreFilesSame(fname, link, options, res, dbg);
  return s;
}

void ZenFS::EncodeSnapshotTo(std::string* output) {
  std::map<std::string, std::shared_ptr<ZoneFile>>::iterator it;
  std::string files_string;
  PutFixed32(output, kCompleteFilesSnapshot);
  for (it = files_.begin(); it != files_.end(); it++) {
    std::string file_string;
    std::shared_ptr<ZoneFile> zFile = it->second;

    zFile->EncodeSnapshotTo(&file_string);
    PutLengthPrefixedSlice(&files_string, Slice(file_string));
  }
  PutLengthPrefixedSlice(output, Slice(files_string));
}

void ZenFS::EncodeJson(std::ostream& json_stream) {
  bool first_element = true;
  json_stream << "[";
  for (const auto& file : files_) {
    if (first_element) {
      first_element = false;
    } else {
      json_stream << ",";
    }
    file.second->EncodeJson(json_stream);
  }
  json_stream << "]";
}

Status ZenFS::DecodeFileUpdateFrom(Slice* slice, bool replace) {
  std::shared_ptr<ZoneFile> update(new ZoneFile(zbd_, 0, &metadata_writer_));
  uint64_t id;
  Status s;

  s = update->DecodeFrom(slice);
  if (!s.ok()) return s;

  id = update->GetID();
  if (id >= next_file_id_) next_file_id_ = id + 1;

  /* Check if this is an update or an replace to an existing file */
  for (auto it = files_.begin(); it != files_.end(); it++) {
    std::shared_ptr<ZoneFile> zFile = it->second;
    if (id == zFile->GetID()) {
      for (const auto& name : zFile->GetLinkFiles()) {
        if (files_.find(name) != files_.end())
          files_.erase(name);
        else
          return Status::Corruption("DecodeFileUpdateFrom: missing link file");
      }

      s = zFile->MergeUpdate(update, replace);
      update.reset();

      if (!s.ok()) return s;

      for (const auto& name : zFile->GetLinkFiles())
        files_.insert(std::make_pair(name, zFile));

      return Status::OK();
    }
  }

  /* The update is a new file */
  assert(GetFile(update->GetFilename()) == nullptr);
  files_.insert(std::make_pair(update->GetFilename(), update));

  return Status::OK();
}

Status ZenFS::DecodeSnapshotFrom(Slice* input) {
  Slice slice;

  assert(files_.size() == 0);

  while (GetLengthPrefixedSlice(input, &slice)) {
    std::shared_ptr<ZoneFile> zoneFile(
        new ZoneFile(zbd_, 0, &metadata_writer_));
    Status s = zoneFile->DecodeFrom(&slice);
    if (!s.ok()) return s;

    if (zoneFile->GetID() >= next_file_id_)
      next_file_id_ = zoneFile->GetID() + 1;

    for (const auto& name : zoneFile->GetLinkFiles())
      files_.insert(std::make_pair(name, zoneFile));
  }

  return Status::OK();
}

void ZenFS::EncodeFileDeletionTo(std::shared_ptr<ZoneFile> zoneFile,
                                 std::string* output, std::string linkf) {
  std::string file_string;

  PutFixed64(&file_string, zoneFile->GetID());
  PutLengthPrefixedSlice(&file_string, Slice(linkf));

  PutFixed32(output, kFileDeletion);
  PutLengthPrefixedSlice(output, Slice(file_string));
}

Status ZenFS::DecodeFileDeletionFrom(Slice* input) {
  uint64_t fileID;
  std::string fileName;
  Slice slice;
  IOStatus s;

  if (!GetFixed64(input, &fileID))
    return Status::Corruption("Zone file deletion: file id missing");

  if (!GetLengthPrefixedSlice(input, &slice))
    return Status::Corruption("Zone file deletion: file name missing");

  fileName = slice.ToString();
  if (files_.find(fileName) == files_.end())
    return Status::Corruption("Zone file deletion: no such file");

  std::shared_ptr<ZoneFile> zoneFile = files_[fileName];
  if (zoneFile->GetID() != fileID)
    return Status::Corruption("Zone file deletion: file ID missmatch");

  files_.erase(fileName);
  s = zoneFile->RemoveLinkName(fileName);
  if (!s.ok())
    return Status::Corruption("Zone file deletion: file links missmatch");

  return Status::OK();
}

Status ZenFS::RecoverFrom(ZenMetaLog* log) {
  bool at_least_one_snapshot = false;
  std::string scratch;
  uint32_t tag = 0;
  Slice record;
  Slice data;
  Status s;
  bool done = false;

  while (!done) {
    IOStatus rs = log->ReadRecord(&record, &scratch);
    if (!rs.ok()) {
      Error(logger_, "Read recovery record failed with error: %s",
            rs.ToString().c_str());
      return Status::Corruption("ZenFS", "Metadata corruption");
    }

    if (!GetFixed32(&record, &tag)) break;

    if (tag == kEndRecord) break;

    if (!GetLengthPrefixedSlice(&record, &data)) {
      return Status::Corruption("ZenFS", "No recovery record data");
    }

    switch (tag) {
      case kCompleteFilesSnapshot:
        ClearFiles();
        s = DecodeSnapshotFrom(&data);
        if (!s.ok()) {
          Warn(logger_, "Could not decode complete snapshot: %s",
               s.ToString().c_str());
          return s;
        }
        at_least_one_snapshot = true;
        break;

      case kFileUpdate:
        s = DecodeFileUpdateFrom(&data);
        if (!s.ok()) {
          Warn(logger_, "Could not decode file snapshot: %s",
               s.ToString().c_str());
          return s;
        }
        break;

      case kFileReplace:
        s = DecodeFileUpdateFrom(&data, true);
        if (!s.ok()) {
          Warn(logger_, "Could not decode file snapshot: %s",
               s.ToString().c_str());
          return s;
        }
        break;

      case kFileDeletion:
        s = DecodeFileDeletionFrom(&data);
        if (!s.ok()) {
          Warn(logger_, "Could not decode file deletion: %s",
               s.ToString().c_str());
          return s;
        }
        break;

      default:
        Warn(logger_, "Unexpected metadata record tag: %u", tag);
        return Status::Corruption("ZenFS", "Unexpected tag");
    }
  }

  if (at_least_one_snapshot)
    return Status::OK();
  else
    return Status::NotFound("ZenFS", "No snapshot found");
}

/* Mount the filesystem by recovering form the latest valid metadata zone */
Status ZenFS::Mount(bool readonly) {
  std::vector<Zone*> metazones = zbd_->GetMetaZones();
  std::vector<std::unique_ptr<Superblock>> valid_superblocks;
  std::vector<std::unique_ptr<ZenMetaLog>> valid_logs;
  std::vector<Zone*> valid_zones;
  std::vector<std::pair<uint32_t, uint32_t>> seq_map;

  Status s;

  /* We need a minimum of two non-offline meta data zones */
  if (metazones.size() < 2) {
    Error(logger_,
          "Need at least two non-offline meta zones to open for write");
    return Status::NotSupported();
  }

  /* Find all valid superblocks */
  for (const auto z : metazones) {
    std::unique_ptr<ZenMetaLog> log;
    std::string scratch;
    Slice super_record;

    if (!z->Acquire()) {
      assert(false);
      return Status::Aborted("Could not aquire busy flag of zone" +
                             std::to_string(z->GetZoneNr()));
    }

    // log takes the ownership of z's busy flag.
    log.reset(new ZenMetaLog(zbd_, z));

    if (!log->ReadRecord(&super_record, &scratch).ok()) continue;

    if (super_record.size() == 0) continue;

    std::unique_ptr<Superblock> super_block;

    super_block.reset(new Superblock());
    s = super_block->DecodeFrom(&super_record);
    if (s.ok()) s = super_block->CompatibleWith(zbd_);
    if (!s.ok()) return s;

    Info(logger_, "Found OK superblock in zone %lu seq: %u\n", z->GetZoneNr(),
         super_block->GetSeq());

    seq_map.push_back(std::make_pair(super_block->GetSeq(), seq_map.size()));
    valid_superblocks.push_back(std::move(super_block));
    valid_logs.push_back(std::move(log));
    valid_zones.push_back(z);
  }

  if (!seq_map.size()) return Status::NotFound("No valid superblock found");

  /* Sort superblocks by descending sequence number */
  std::sort(seq_map.begin(), seq_map.end(),
            std::greater<std::pair<uint32_t, uint32_t>>());

  bool recovery_ok = false;
  unsigned int r = 0;

  /* Recover from the zone with the highest superblock sequence number.
     If that fails go to the previous as we might have crashed when rolling
     metadata zone.
  */
  for (const auto& sm : seq_map) {
    uint32_t i = sm.second;
    std::string scratch;
    std::unique_ptr<ZenMetaLog> log = std::move(valid_logs[i]);

    s = RecoverFrom(log.get());
    if (!s.ok()) {
      if (s.IsNotFound()) {
        Warn(logger_,
             "Did not find a valid snapshot, trying next meta zone. Error: %s",
             s.ToString().c_str());
        continue;
      }

      Error(logger_, "Metadata corruption. Error: %s", s.ToString().c_str());
      return s;
    }

    r = i;
    recovery_ok = true;
    meta_log_ = std::move(log);
    break;
  }

  if (!recovery_ok) {
    return Status::IOError("Failed to mount filesystem");
  }

  Info(logger_, "Recovered from zone: %d", (int)valid_zones[r]->GetZoneNr());
  superblock_ = std::move(valid_superblocks[r]);
  zbd_->SetFinishTreshold(superblock_->GetFinishTreshold());

  IOOptions foo;
  IODebugContext bar;
  s = target()->CreateDirIfMissing(superblock_->GetAuxFsPath(), foo, &bar);
  if (!s.ok()) {
    Error(logger_, "Failed to create aux filesystem directory.");
    return s;
  }

  /* Free up old metadata zones, to get ready to roll */
  for (const auto& sm : seq_map) {
    uint32_t i = sm.second;
    /* Don't reset the current metadata zone */
    if (i != r) {
      /* Metadata zones are not marked as having valid data, so they can be
       * reset */
      valid_logs[i].reset();
    }
  }

  s = Repair();
  if (!s.ok()) return s;

  if (readonly) {
    Info(logger_, "Mounting READ ONLY");
  } else {
    std::lock_guard<std::mutex> lock(files_mtx_);
    s = RollMetaZoneLocked();
    if (!s.ok()) {
      Error(logger_, "Failed to roll metadata zone.");
      return s;
    }
  }

  Info(logger_, "Superblock sequence %d", (int)superblock_->GetSeq());
  Info(logger_, "Finish threshold %u", superblock_->GetFinishTreshold());
  Info(logger_, "Filesystem mount OK");

  if (!readonly) {
    Info(logger_, "Resetting unused IO Zones..");
    IOStatus status = zbd_->ResetUnusedIOZones();
    if (!status.ok()) return status;
    Info(logger_, "  Done");
  }

  LogFiles();

  return Status::OK();
}

Status ZenFS::MkFS(std::string aux_fs_p, uint32_t finish_threshold) {
  std::vector<Zone*> metazones = zbd_->GetMetaZones();
  std::unique_ptr<ZenMetaLog> log;
  Zone* meta_zone = nullptr;
  std::string aux_fs_path = FormatPathLexically(aux_fs_p);
  IOStatus s;

  if (aux_fs_path.length() > 255) {
    return Status::InvalidArgument(
        "Aux filesystem path must be less than 256 bytes\n");
  }

  ClearFiles();
  IOStatus status = zbd_->ResetUnusedIOZones();
  if (!status.ok()) return status;

  for (const auto mz : metazones) {
    if (!mz->Acquire()) {
      assert(false);
      return Status::Aborted("Could not aquire busy flag of zone " +
                             std::to_string(mz->GetZoneNr()));
    }

    if (mz->Reset().ok()) {
      if (!meta_zone) meta_zone = mz;
    } else {
      Warn(logger_, "Failed to reset meta zone\n");
    }

    if (meta_zone != mz) {
      // for meta_zone == mz the ownership of mz's busy flag is passed to log.
      if (!mz->Release()) {
        assert(false);
        return Status::Aborted("Could not unset busy flag of zone " +
                               std::to_string(mz->GetZoneNr()));
      }
    }
  }

  if (!meta_zone) {
    return Status::IOError("Not available meta zones\n");
  }

  log.reset(new ZenMetaLog(zbd_, meta_zone));

  Superblock super(zbd_, aux_fs_path, finish_threshold);
  std::string super_string;
  super.EncodeTo(&super_string);

  s = log->AddRecord(super_string);
  if (!s.ok()) return std::move(s);

  /* Write an empty snapshot to make the metadata zone valid */
  s = PersistSnapshot(log.get());
  if (!s.ok()) {
    Error(logger_, "Failed to persist snapshot: %s", s.ToString().c_str());
    return Status::IOError("Failed persist snapshot");
  }

  Info(logger_, "Empty filesystem created");
  return Status::OK();
}

std::map<std::string, Env::WriteLifeTimeHint> ZenFS::GetWriteLifeTimeHints() {
  std::map<std::string, Env::WriteLifeTimeHint> hint_map;

  for (auto it = files_.begin(); it != files_.end(); it++) {
    std::shared_ptr<ZoneFile> zoneFile = it->second;
    std::string filename = it->first;
    hint_map.insert(std::make_pair(filename, zoneFile->GetWriteLifeTimeHint()));
  }

  return hint_map;
}

#if !defined(NDEBUG) || defined(WITH_TERARKDB)
static std::string GetLogFilename(std::string bdev) {
  std::ostringstream ss;
  time_t t = time(0);
  struct tm* log_start = std::localtime(&t);
  char buf[40];

  std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H:%M:%S.log", log_start);
  ss << DEFAULT_ZENV_LOG_PATH << std::string("zenfs_") << bdev << "_" << buf;

  return ss.str();
}
#endif

Status NewZenFS(FileSystem** fs, const std::string& bdevname,
                std::shared_ptr<ZenFSMetrics> metrics) {
  std::shared_ptr<Logger> logger;
  Status s;

  // TerarkDB needs to log important information in production while ZenFS
  // doesn't (currently).
  //
  // TODO(guokuankuan@bytedance.com) We need to figure out how to reuse
  // RocksDB's logger in the future.
#if !defined(NDEBUG) || defined(WITH_TERARKDB)
  s = Env::Default()->NewLogger(GetLogFilename(bdevname), &logger);
  if (!s.ok()) {
    fprintf(stderr, "ZenFS: Could not create logger");
  } else {
    logger->SetInfoLogLevel(DEBUG_LEVEL);
#ifdef WITH_TERARKDB
    logger->SetInfoLogLevel(INFO_LEVEL);
#endif
  }
#endif

  ZonedBlockDevice* zbd = new ZonedBlockDevice(bdevname, logger, metrics);
  IOStatus zbd_status = zbd->Open(false, true);
  if (!zbd_status.ok()) {
    Error(logger, "mkfs: Failed to open zoned block device: %s",
          zbd_status.ToString().c_str());
    return Status::IOError(zbd_status.ToString());
  }

  ZenFS* zenFS = new ZenFS(zbd, FileSystem::Default(), logger);
  s = zenFS->Mount(false);
  if (!s.ok()) {
    delete zenFS;
    return s;
  }

  *fs = zenFS;
  return Status::OK();
}

Status ListZenFileSystems(std::map<std::string, std::string>& out_list) {
  std::map<std::string, std::string> zenFileSystems;

  auto closedirDeleter = [](DIR* d) {
    if (d != nullptr) closedir(d);
  };
  std::unique_ptr<DIR, decltype(closedirDeleter)> dir{
      opendir("/sys/class/block"), std::move(closedirDeleter)};
  struct dirent* entry;

  while (NULL != (entry = readdir(dir.get()))) {
    if (entry->d_type == DT_LNK) {
      std::string zbdName = std::string(entry->d_name);
      std::unique_ptr<ZonedBlockDevice> zbd{
          new ZonedBlockDevice(zbdName, nullptr)};
      IOStatus zbd_status = zbd->Open(true, false);

      if (zbd_status.ok()) {
        std::vector<Zone*> metazones = zbd->GetMetaZones();
        std::string scratch;
        Slice super_record;
        Status s;

        for (const auto z : metazones) {
          Superblock super_block;
          std::unique_ptr<ZenMetaLog> log;
          if (!z->Acquire()) {
            return Status::Aborted("Could not aquire busy flag of zone" +
                                   std::to_string(z->GetZoneNr()));
          }
          log.reset(new ZenMetaLog(zbd.get(), z));

          if (!log->ReadRecord(&super_record, &scratch).ok()) continue;
          s = super_block.DecodeFrom(&super_record);
          if (s.ok()) {
            /* Map the uuid to the device-mapped (i.g dm-linear) block device to
               avoid trying to mount the whole block device in case of a split
               device */
            if (zenFileSystems.find(super_block.GetUUID()) !=
                    zenFileSystems.end() &&
                zenFileSystems[super_block.GetUUID()].rfind("dm-", 0) == 0) {
              break;
            }
            zenFileSystems[super_block.GetUUID()] = zbdName;
            break;
          }
        }
        continue;
      }
    }
  }

  out_list = std::move(zenFileSystems);
  return Status::OK();
}

void ZenFS::GetZenFSSnapshot(ZenFSSnapshot& snapshot,
                             const ZenFSSnapshotOptions& options) {
  if (options.zbd_) {
    snapshot.zbd_ = ZBDSnapshot(*zbd_);
  }
  if (options.zone_) {
    zbd_->GetZoneSnapshot(snapshot.zones_);
  }
  if (options.zone_file_) {
    std::lock_guard<std::mutex> file_lock(files_mtx_);
    for (const auto& file_it : files_) {
      ZoneFile& file = *(file_it.second);

      /* Skip files open for writing, as extents are being updated */
      if (!file.TryAcquireWRLock()) continue;

      // file -> extents mapping
      snapshot.zone_files_.emplace_back(file);
      // extent -> file mapping
      for (auto* ext : file.GetExtents()) {
        snapshot.extents_.emplace_back(*ext, file.GetFilename());
      }

      file.ReleaseWRLock();
    }
  }

  if (options.trigger_report_) {
    zbd_->GetMetrics()->ReportSnapshot(snapshot);
  }

  if (options.log_garbage_) {
    zbd_->LogGarbageInfo();
  }
}

IOStatus ZenFS::MigrateExtents(
    const std::vector<ZoneExtentSnapshot*>& extents) {
  IOStatus s;
  // Group extents by their filename
  std::map<std::string, std::vector<ZoneExtentSnapshot*>> file_extents;
  for (auto* ext : extents) {
    std::string fname = ext->filename;
    // We only migrate SST file extents
    if (ends_with(fname, ".sst")) {
      file_extents[fname].emplace_back(ext);
    }
  }

  for (const auto& it : file_extents) {
    s = MigrateFileExtents(it.first, it.second);
    if (!s.ok()) break;
    s = zbd_->ResetUnusedIOZones();
    if (!s.ok()) break;
  }
  return s;
}

IOStatus ZenFS::MigrateFileExtents(
    const std::string& fname,
    const std::vector<ZoneExtentSnapshot*>& migrate_exts) {
  IOStatus s = IOStatus::OK();
  Info(logger_, "MigrateFileExtents, fname: %s, extent count: %lu",
       fname.data(), migrate_exts.size());

  // The file may be deleted by other threads, better double check.
  auto zfile = GetFile(fname);
  if (zfile == nullptr) {
    return IOStatus::OK();
  }

  // Don't migrate open for write files and prevent write reopens while we
  // migrate
  if (!zfile->TryAcquireWRLock()) {
    return IOStatus::OK();
  }

  std::vector<ZoneExtent*> new_extent_list;
  std::vector<ZoneExtent*> extents = zfile->GetExtents();
  for (const auto* ext : extents) {
    new_extent_list.push_back(
        new ZoneExtent(ext->start_, ext->length_, ext->zone_));
  }

  // Modify the new extent list
  for (ZoneExtent* ext : new_extent_list) {
    // Check if current extent need to be migrated
    auto it = std::find_if(migrate_exts.begin(), migrate_exts.end(),
                           [&](const ZoneExtentSnapshot* ext_snapshot) {
                             return ext_snapshot->start == ext->start_ &&
                                    ext_snapshot->length == ext->length_;
                           });

    if (it == migrate_exts.end()) {
      Info(logger_, "Migrate extent not found, ext_start: %lu", ext->start_);
      continue;
    }

    Zone* target_zone = nullptr;

    // Allocate a new migration zone.
    s = zbd_->TakeMigrateZone(&target_zone, zfile->GetWriteLifeTimeHint(),
                              ext->length_);
    if (!s.ok()) {
      continue;
    }

    if (target_zone == nullptr) {
      zbd_->ReleaseMigrateZone(target_zone);
      Info(logger_, "Migrate Zone Acquire Failed, Ignore Task.");
      continue;
    }

    uint64_t target_start = target_zone->wp_;
    if (zfile->IsSparse()) {
      // For buffered write, ZenFS use inlined metadata for extents and each
      // extent has a SPARSE_HEADER_SIZE.
      target_start = target_zone->wp_ + ZoneFile::SPARSE_HEADER_SIZE;
      zfile->MigrateData(ext->start_ - ZoneFile::SPARSE_HEADER_SIZE,
                         ext->length_ + ZoneFile::SPARSE_HEADER_SIZE,
                         target_zone);
      zbd_->AddGCBytesWritten(ext->length_ + ZoneFile::SPARSE_HEADER_SIZE);
    } else {
      zfile->MigrateData(ext->start_, ext->length_, target_zone);
      zbd_->AddGCBytesWritten(ext->length_);
    }

    // If the file doesn't exist, skip
    if (GetFileNoLock(fname) == nullptr) {
      Info(logger_, "Migrate file not exist anymore.");
      zbd_->ReleaseMigrateZone(target_zone);
      break;
    }

    ext->start_ = target_start;
    ext->zone_ = target_zone;
    ext->zone_->used_capacity_ += ext->length_;

    zbd_->ReleaseMigrateZone(target_zone);
  }

  SyncFileExtents(zfile.get(), new_extent_list);
  zfile->ReleaseWRLock();

  Info(logger_, "MigrateFileExtents Finished, fname: %s, extent count: %lu",
       fname.data(), migrate_exts.size());
  return IOStatus::OK();
}

extern "C" FactoryFunc<FileSystem> zenfs_filesystem_reg;

FactoryFunc<FileSystem> zenfs_filesystem_reg =
#if (ROCKSDB_MAJOR < 6) || (ROCKSDB_MAJOR <= 6 && ROCKSDB_MINOR < 28)
    ObjectLibrary::Default()->Register<FileSystem>(
        "zenfs://.*", [](const std::string& uri, std::unique_ptr<FileSystem>* f,
                         std::string* errmsg) {
#else
    ObjectLibrary::Default()->AddFactory<FileSystem>(
        ObjectLibrary::PatternEntry("zenfs", false)
            .AddSeparator("://"), /* "zenfs://.+" */
        [](const std::string& uri, std::unique_ptr<FileSystem>* f,
           std::string* errmsg) {
#endif
          std::string devID = uri;
          FileSystem* fs = nullptr;
          Status s;

          devID.replace(0, strlen("zenfs://"), "");
          if (devID.rfind("dev:") == 0) {
            devID.replace(0, strlen("dev:"), "");
            s = NewZenFS(&fs, devID);
            if (!s.ok()) {
              *errmsg = s.ToString();
            }
          } else if (devID.rfind("uuid:") == 0) {
            std::map<std::string, std::string> zenFileSystems;
            s = ListZenFileSystems(zenFileSystems);
            if (!s.ok()) {
              *errmsg = s.ToString();
            } else {
              devID.replace(0, strlen("uuid:"), "");

              if (zenFileSystems.find(devID) == zenFileSystems.end()) {
                *errmsg = "UUID not found";
              } else {
                s = NewZenFS(&fs, zenFileSystems[devID]);
                if (!s.ok()) {
                  *errmsg = s.ToString();
                }
              }
            }
          } else {
            *errmsg = "Malformed URI";
          }
          f->reset(fs);
          return f->get();
        });
};  // namespace ROCKSDB_NAMESPACE

#else

#include "rocksdb/env.h"

namespace ROCKSDB_NAMESPACE {
Status NewZenFS(FileSystem** /*fs*/, const std::string& /*bdevname*/,
                ZenFSMetrics* /*metrics*/) {
  return Status::NotSupported("Not built with ZenFS support\n");
}
std::map<std::string, std::string> ListZenFileSystems() {
  std::map<std::string, std::string> zenFileSystems;
  return zenFileSystems;
}
}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && defined(OS_LINUX)
