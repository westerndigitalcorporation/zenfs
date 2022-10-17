// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#if !defined(ROCKSDB_LITE) && defined(OS_LINUX)

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rocksdb/io_status.h"
#include "zbd_zenfs.h"

namespace ROCKSDB_NAMESPACE {

class ZbdlibBackend : public ZonedBlockDeviceBackend {
 private:
  std::string filename_;
  int read_f_;
  int read_direct_f_;
  int write_f_;

 public:
  explicit ZbdlibBackend(std::string bdevname);
  ~ZbdlibBackend() {
    zbd_close(read_f_);
    zbd_close(read_direct_f_);
    zbd_close(write_f_);
  }

  IOStatus Open(bool readonly, bool exclusive, unsigned int *max_active_zones,
                unsigned int *max_open_zones);
  std::unique_ptr<ZoneList> ListZones();
  IOStatus Reset(uint64_t start, bool *offline, uint64_t *max_capacity);
  IOStatus Finish(uint64_t start);
  IOStatus Close(uint64_t start);
  int Read(char *buf, int size, uint64_t pos, bool direct);
  int Write(char *data, uint32_t size, uint64_t pos);
  int InvalidateCache(uint64_t pos, uint64_t size);

  bool ZoneIsSwr(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_type(z) == ZBD_ZONE_TYPE_SWR;
  };

  bool ZoneIsOffline(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_offline(z);
  };

  bool ZoneIsWritable(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return !(zbd_zone_full(z) || zbd_zone_offline(z) || zbd_zone_rdonly(z));
  };

  bool ZoneIsActive(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_imp_open(z) || zbd_zone_exp_open(z) || zbd_zone_closed(z);
  };

  bool ZoneIsOpen(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_imp_open(z) || zbd_zone_exp_open(z);
  };

  uint64_t ZoneStart(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_start(z);
  };

  uint64_t ZoneMaxCapacity(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_capacity(z);
  };

  uint64_t ZoneWp(std::unique_ptr<ZoneList> &zones, unsigned int idx) {
    struct zbd_zone *z = &((struct zbd_zone *)zones->GetData())[idx];
    return zbd_zone_wp(z);
  };

  std::string GetFilename() { return filename_; }

 private:
  IOStatus CheckScheduler();
  std::string ErrorToString(int err);
};

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && defined(OS_LINUX)
