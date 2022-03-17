
#include "util.h"

#include <libzbd/zbd.h>
#include <unistd.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>

#include "fs_zenfs.h"

namespace ROCKSDB_NAMESPACE {

IOStatus zenfs_zbd_open(std::filesystem::path const &path, bool readonly,
                        bool exclusive,
                        std::unique_ptr<ZonedBlockDevice> &out_zbd) {
  out_zbd =
      std::unique_ptr<ZonedBlockDevice>{new ZonedBlockDevice(path, nullptr)};
  IOStatus status = out_zbd->Open(readonly, exclusive);

  if (!status.ok()) {
    fprintf(stderr, "Failed to open zoned block device: %s, error: %s\n",
            path.c_str(), status.ToString().c_str());
    out_zbd.reset();
  }

  return status;
}

Status zenfs_mount(std::unique_ptr<ZonedBlockDevice> &zbd, bool readonly,
                   std::unique_ptr<ZenFS> &out_zen_fs) {
  Status status;

  out_zen_fs = std::unique_ptr<ZenFS>{
      new ZenFS(std::move(zbd), FileSystem::Default(), nullptr)};
  status = out_zen_fs->Mount(readonly);
  if (!status.ok()) {
    out_zen_fs.reset();
  }

  return status;
}

static Status zenfs_exists(std::filesystem::path const &zbd_path,
                           bool &out_exists) {
  Status status;
  std::unique_ptr<ZonedBlockDevice> zbd;
  std::unique_ptr<ZenFS> zen_fs;

  status = zenfs_zbd_open(zbd_path, false, true, zbd);
  if (!status.ok()) return Status::IOError("Failed to open ZBD");

  status = zenfs_mount(zbd, false, zen_fs);
  out_exists = status.ok() || !status.IsNotFound();

  return Status::OK();
}

// Create or check pre-existing aux directory and fail if it is
// inaccessible by current user and if it has previous data
static Status create_aux_dir(std::filesystem::path const &path) {
  namespace fs = std::filesystem;
  std::error_code ec;

  bool aux_exists = fs::exists(path, ec);
  if (ec) {
    return Status::Aborted("Failed to check if aux directory exists: " +
                           ec.message());
  }

  bool aux_is_dir = false;
  if (aux_exists) {
    aux_is_dir = fs::is_directory(path, ec);
    if (ec) {
      return Status::Aborted("Failed to check if aux_dir is directory" +
                             ec.message());
    }
  }

  if (aux_exists && !aux_is_dir) {
    return Status::Aborted("Aux path exists but is not a directory");
  }

  if (!aux_exists) {
    if (!fs::create_directory(path, ec) || ec) {
      return Status::IOError("Failed to create aux path:" + ec.message());
    }

    fs::permissions(
        path,
        fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec,
        ec);
    if (ec) {
      return Status::IOError("Failed to set permissions on aux path:" +
                             ec.message());
    }
  }

  if (access(path.c_str(), R_OK | W_OK | X_OK) < 0) {
    return Status::Aborted(
        "User does not have access permissions on aux directory " +
        path.string());
  }

  if (std::distance(fs::directory_iterator{path}, {}) > 2) {
    return Status::Aborted("Aux directory " + path.string() + " is not empty");
  }

  return Status::OK();
}

static Status zenfs_create(std::filesystem::path const &zbd_path,
                           std::filesystem::path const &aux_path,
                           uint32_t finish_threshold

) {
  Status status;
  std::unique_ptr<ZonedBlockDevice> zbd;
  std::unique_ptr<ZenFS> zen_fs;

  status = create_aux_dir(aux_path);
  if (!status.ok()) return status;

  status = zenfs_zbd_open(zbd_path, false, true, zbd);
  if (!status.ok()) {
    return status;
  }

  zen_fs.reset(new ZenFS(std::move(zbd), FileSystem::Default(), nullptr));

  std::string aux_path_patched = aux_path;
  if (aux_path_patched.back() != '/') aux_path_patched.append("/");

  status = zen_fs->MkFS(aux_path_patched, finish_threshold);
  if (!status.ok()) {
    return status;
  }

  return status;
}

Status zenfs_mkfs(std::filesystem::path const &zbd_path,
                  std::filesystem::path const &aux_path,
                  uint32_t finish_threshold, bool force) {
  Status status;
  bool exists = false;

  status = zenfs_exists(zbd_path, exists);
  if (!status.ok()) {
    return status;
  }

  if (exists && !force) {
    return Status::Aborted(
        "Existing filesystem found, use --force if you want to replace "
        "it.");
  }

  return zenfs_create(zbd_path, aux_path, finish_threshold);
}

}  // namespace ROCKSDB_NAMESPACE
