// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2019-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#if !defined(ROCKSDB_LITE) && !defined(OS_WIN)

#include "zbdlib_zenfs.h"

#include <errno.h>
#include <fcntl.h>
#include <libzbd/zbd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fstream>
#include <string>

#include "rocksdb/env.h"
#include "rocksdb/io_status.h"

namespace ROCKSDB_NAMESPACE {

ZbdlibBackend::ZbdlibBackend(std::string bdevname)
    : filename_("/dev/" + bdevname),
      read_f_(-1),
      read_direct_f_(-1),
      write_f_(-1) {}

std::string ZbdlibBackend::ErrorToString(int err) {
  char *err_str = strerror(err);
  if (err_str != nullptr) return std::string(err_str);
  return "";
}

IOStatus ZbdlibBackend::CheckScheduler() {
  std::ostringstream path;
  std::string s = filename_;
  std::fstream f;

  s.erase(0, 5);  // Remove "/dev/" from /dev/nvmeXnY
  path << "/sys/block/" << s << "/queue/scheduler";
  f.open(path.str(), std::fstream::in);
  if (!f.is_open()) {
    return IOStatus::InvalidArgument("Failed to open " + path.str());
  }

  std::string buf;
  getline(f, buf);
  if (buf.find("[mq-deadline]") == std::string::npos) {
    f.close();
    return IOStatus::InvalidArgument(
        "Current ZBD scheduler is not mq-deadline, set it to mq-deadline.");
  }

  f.close();
  return IOStatus::OK();
}

IOStatus ZbdlibBackend::Open(bool readonly, bool exclusive,
                             unsigned int *max_active_zones,
                             unsigned int *max_open_zones) {
  zbd_info info;

  /* The non-direct file descriptor acts as an exclusive-use semaphore */
  if (exclusive) {
    read_f_ = zbd_open(filename_.c_str(), O_RDONLY | O_EXCL, &info);
  } else {
    read_f_ = zbd_open(filename_.c_str(), O_RDONLY, &info);
  }

  if (read_f_ < 0) {
    return IOStatus::InvalidArgument(
        "Failed to open zoned block device for read: " + ErrorToString(errno));
  }

  read_direct_f_ = zbd_open(filename_.c_str(), O_RDONLY | O_DIRECT, &info);
  if (read_direct_f_ < 0) {
    return IOStatus::InvalidArgument(
        "Failed to open zoned block device for direct read: " +
        ErrorToString(errno));
  }

  if (readonly) {
    write_f_ = -1;
  } else {
    write_f_ = zbd_open(filename_.c_str(), O_WRONLY | O_DIRECT, &info);
    if (write_f_ < 0) {
      return IOStatus::InvalidArgument(
          "Failed to open zoned block device for write: " +
          ErrorToString(errno));
    }
  }

  if (info.model != ZBD_DM_HOST_MANAGED) {
    return IOStatus::NotSupported("Not a host managed block device");
  }

  IOStatus ios = CheckScheduler();
  if (ios != IOStatus::OK()) return ios;

  block_sz_ = info.pblock_size;
  zone_sz_ = info.zone_size;
  nr_zones_ = info.nr_zones;
  *max_active_zones = info.max_nr_active_zones;
  *max_open_zones = info.max_nr_open_zones;
  return IOStatus::OK();
}

std::unique_ptr<ZoneList> ZbdlibBackend::ListZones() {
  int ret;
  void *zones;
  unsigned int nr_zones;

  ret = zbd_list_zones(read_f_, 0, zone_sz_ * nr_zones_, ZBD_RO_ALL,
                       (struct zbd_zone **)&zones, &nr_zones);
  if (ret) {
    return nullptr;
  }

  std::unique_ptr<ZoneList> zl(new ZoneList(zones, nr_zones));

  return zl;
}

IOStatus ZbdlibBackend::Reset(uint64_t start, bool *offline,
                              uint64_t *max_capacity) {
  unsigned int report = 1;
  struct zbd_zone z;
  int ret;

  ret = zbd_reset_zones(write_f_, start, zone_sz_);
  if (ret) return IOStatus::IOError("Zone reset failed\n");

  ret = zbd_report_zones(read_f_, start, zone_sz_, ZBD_RO_ALL, &z, &report);

  if (ret || (report != 1)) return IOStatus::IOError("Zone report failed\n");

  if (zbd_zone_offline(&z)) {
    *offline = true;
    *max_capacity = 0;
  } else {
    *offline = false;
    *max_capacity = zbd_zone_capacity(&z);
  }

  return IOStatus::OK();
}

IOStatus ZbdlibBackend::Finish(uint64_t start) {
  int ret;

  ret = zbd_finish_zones(write_f_, start, zone_sz_);
  if (ret) return IOStatus::IOError("Zone finish failed\n");

  return IOStatus::OK();
}

IOStatus ZbdlibBackend::Close(uint64_t start) {
  int ret;

  ret = zbd_close_zones(write_f_, start, zone_sz_);
  if (ret) return IOStatus::IOError("Zone close failed\n");

  return IOStatus::OK();
}

int ZbdlibBackend::InvalidateCache(uint64_t pos, uint64_t size) {
  return posix_fadvise(read_f_, pos, size, POSIX_FADV_DONTNEED);
}

int ZbdlibBackend::Read(char *buf, int size, uint64_t pos, bool direct) {
  return pread(direct ? read_direct_f_ : read_f_, buf, size, pos);
}

int ZbdlibBackend::Write(char *data, uint32_t size, uint64_t pos) {
  return pwrite(write_f_, data, size, pos);
}

}  // namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && !defined(OS_WIN)
