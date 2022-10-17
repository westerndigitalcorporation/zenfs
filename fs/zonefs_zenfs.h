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

#include <list>
#include <map>
#include <string>

#include "rocksdb/io_status.h"
#include "zbd_zenfs.h"

namespace ROCKSDB_NAMESPACE {

class ZoneFsFile {
 private:
  int fd_;

 public:
  ZoneFsFile(int fd) : fd_(fd) {}

  ~ZoneFsFile() { close(fd_); }

  int GetFd() { return fd_; }
};

class ZoneFsFileCache {
 private:
  std::list<std::pair<uint64_t, std::shared_ptr<ZoneFsFile>>> list_;
  std::unordered_map<
      uint64_t,
      std::list<std::pair<uint64_t, std::shared_ptr<ZoneFsFile>>>::iterator>
      map_;
  std::mutex mtx_;
  unsigned max_;
  int flags_;

 public:
  explicit ZoneFsFileCache(int flags);
  ~ZoneFsFileCache();

  void Put(uint64_t zone);
  std::shared_ptr<ZoneFsFile> Get(uint64_t zone, std::string filename);
  void Resize(unsigned new_size);

 private:
  void Prune(unsigned limit);
};

class ZoneFsBackend : public ZonedBlockDeviceBackend {
 private:
  std::string mountpoint_;
  int zone_zero_fd_;
  bool readonly_;
  ZoneFsFileCache rd_fds_;
  ZoneFsFileCache direct_rd_fds_;
  ZoneFsFileCache wr_fds_;

 public:
  explicit ZoneFsBackend(std::string mountpoint);
  ~ZoneFsBackend();

  IOStatus Open(bool readonly, bool exclusive, unsigned int *max_active_zones,
                unsigned int *max_open_zones);
  std::unique_ptr<ZoneList> ListZones();
  IOStatus Reset(uint64_t start, bool *offline, uint64_t *max_capacity);
  IOStatus Finish(uint64_t start);
  IOStatus Close(uint64_t start);
  int Read(char *buf, int size, uint64_t pos, bool direct);
  int Write(char *data, uint32_t size, uint64_t pos);
  int InvalidateCache(uint64_t pos, uint64_t size);

  bool ZoneIsSwr(std::unique_ptr<ZoneList> &zones, unsigned int idx);
  bool ZoneIsOffline(std::unique_ptr<ZoneList> &zones, unsigned int idx);
  bool ZoneIsWritable(std::unique_ptr<ZoneList> &zones, unsigned int idx);
  bool ZoneIsActive(std::unique_ptr<ZoneList> &zones, unsigned int idx);
  bool ZoneIsOpen(std::unique_ptr<ZoneList> &zones2, unsigned int idx);
  uint64_t ZoneStart(std::unique_ptr<ZoneList> &zones, unsigned int idx);
  uint64_t ZoneMaxCapacity(std::unique_ptr<ZoneList> &zones, unsigned int idx);
  uint64_t ZoneWp(std::unique_ptr<ZoneList> &zones, unsigned int idx);
  std::string GetFilename() { return mountpoint_; }

 private:
  std::string ErrorToString(int err);
  uint64_t LBAToZoneOffset(uint64_t pos);
  std::string LBAToZoneFile(uint64_t start);
  std::string GetBackingDevice(const char *mountpoint);
  unsigned int GetSysFsValue(std::string dev_name, std::string field);
  std::shared_ptr<ZoneFsFile> GetZoneFile(uint64_t start, int flags);
  void PutZoneFile(uint64_t start, int flags);
};

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && defined(OS_LINUX)
