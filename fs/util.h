#pragma once

#include <cstdint>
#include <filesystem>

#include "fs_zenfs.h"
#include "rocksdb/status.h"

namespace ROCKSDB_NAMESPACE {

std::unique_ptr<ZonedBlockDevice> zbd_open(std::string const& zbd_path,
                                           bool readonly, bool exclusive);

Status zenfs_mount(std::unique_ptr<ZonedBlockDevice>& zbd,
                   std::unique_ptr<ZenFS>* zenFS, bool readonly);

int zenfs_mkfs(std::string const& zbd_path, std::string const& aux_path,
               int finish_threshold, bool force);
}  // namespace ROCKSDB_NAMESPACE
