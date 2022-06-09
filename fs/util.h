#pragma once

#include <cstdint>

#include "filesystem.h"
#include "fs_zenfs.h"
#include "rocksdb/status.h"

namespace ROCKSDB_NAMESPACE {

IOStatus zenfs_zbd_open(fs::path const &path, bool readonly,
                        bool exclusive,
                        std::unique_ptr<ZonedBlockDevice> &out_zbd);

Status zenfs_mount(std::unique_ptr<ZonedBlockDevice> &zbd, bool readonly,
                   std::unique_ptr<ZenFS> &out_zen_fs);

Status zenfs_mkfs(fs::path const &zbd_path,
                  fs::path const &aux_path,
                  uint32_t finish_threshold, bool force);
}  // namespace ROCKSDB_NAMESPACE
