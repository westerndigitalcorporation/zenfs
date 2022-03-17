#pragma once

#include <cstdint>
#include <filesystem>

#include "fs_zenfs.h"
#include "rocksdb/status.h"

namespace ROCKSDB_NAMESPACE {

IOStatus zenfs_zbd_open(std::filesystem::path const &path, bool readonly,
                        bool exclusive,
                        std::unique_ptr<ZonedBlockDevice> &out_zbd);

Status zenfs_mount(std::unique_ptr<ZonedBlockDevice> &zbd, bool readonly,
                   std::unique_ptr<ZenFS> &out_zen_fs);

Status zenfs_mkfs(std::filesystem::path const &zbd_path,
                  std::filesystem::path const &aux_path,
                  uint32_t finish_threshold, bool force);
}  // namespace ROCKSDB_NAMESPACE
