// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
// Copyright (c) 2022-present, Western Digital Corporation
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#if !defined(ROCKSDB_LITE) && !defined(OS_WIN)

#include "zonefs_zenfs.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "rocksdb/env.h"
#include "rocksdb/io_status.h"

#define ZENFS_ZONEFS_ZONE_OFFLINE(_f_mode) \
  (((_f_mode) &                            \
    (S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH)) == 0)

namespace ROCKSDB_NAMESPACE {

ZoneFsBackend::ZoneFsBackend(std::string mountpoint)
    : mountpoint_(mountpoint), readonly_(false) {}

ZoneFsBackend::~ZoneFsBackend() {}

std::string ZoneFsBackend::ErrorToString(int err) {
  char *err_str = strerror(err);
  if (err_str != nullptr) return std::string(err_str);
  return "";
}

std::string ZoneFsBackend::LBAToZoneFile(uint64_t start) {
  return mountpoint_ + "/seq/" + std::to_string(start / zone_sz_);
}

uint64_t ZoneFsBackend::LBAToZoneOffset(uint64_t pos) {
  return pos - ((pos / zone_sz_) * zone_sz_);
}

IOStatus ZoneFsBackend::Open(bool readonly,
                             __attribute__((unused)) bool exclusive,
                             uint32_t *block_size, uint64_t *zone_size,
                             uint32_t *nr_zones, unsigned int *max_active_zones,
                             unsigned int *max_open_zones) {
  struct stat zonefs_stat;

  std::string seqdirname = mountpoint_ + "/seq";
  if (stat(seqdirname.c_str(), &zonefs_stat) == -1) {
    return IOStatus::InvalidArgument(
        "Failed to access zonefs sequential zone directory: " +
        ErrorToString(errno));
  }

  // The size of the "seq" directory shows the number of sequential zones
  nr_zones_ = zonefs_stat.st_size;

  seqdirname += "/0";
  if (stat(seqdirname.c_str(), &zonefs_stat) == -1) {
    return IOStatus::InvalidArgument(
        "Failed to access zonefs sequential zone 0: " + ErrorToString(errno));
  }
  zone_sz_ = zonefs_stat.st_blocks * 512;
  block_sz_ = zonefs_stat.st_blksize;

  *block_size = block_sz_;
  *zone_size = zone_sz_;
  *nr_zones = nr_zones_;
  *max_active_zones = 8;  // TODO: read from sysfs
  *max_open_zones = 8;    // TODO: read from sysfs

  readonly_ = readonly;

  return IOStatus::OK();
}

std::unique_ptr<ZoneList> ZoneFsBackend::ListZones() {
  uint32_t i;
  struct stat *z = (struct stat *)calloc(nr_zones_, sizeof(struct stat));
  if (!z) {
    return nullptr;
  }

  for (i = 0; i < nr_zones_; i++) {
    std::string filename = mountpoint_ + "/seq/" + std::to_string(i);
    if (stat(filename.c_str(), &z[i]) < 0) {
      free(z);
      return nullptr;
    }
  }

  std::unique_ptr<ZoneList> zl(new ZoneList((void *)z, nr_zones_));

  return zl;
}

IOStatus ZoneFsBackend::Reset(uint64_t start, bool *offline,
                              uint64_t *max_capacity) {
  int ret;
  struct stat file_stat;

  std::string filename = LBAToZoneFile(start);

  ret = truncate(filename.c_str(), 0);
  if (ret) {
    return IOStatus::IOError("Zone reset failed: " + ErrorToString(errno));
  }

  if (stat(filename.c_str(), &file_stat) < 0) {
    return IOStatus::InvalidArgument(
        "Failed to access zonefs sequential zone " + filename + ": " +
        ErrorToString(errno));
  }

  if (ZENFS_ZONEFS_ZONE_OFFLINE(file_stat.st_mode)) {
    *offline = true;
    *max_capacity = 0;
  } else {
    *offline = false;
    *max_capacity = zone_sz_;
  }

  return IOStatus::OK();
}

IOStatus ZoneFsBackend::Finish(uint64_t start) {
  int ret;

  std::string filename = LBAToZoneFile(start);

  ret = truncate(filename.c_str(), zone_sz_);
  if (ret)
    return IOStatus::IOError("Zone finish failed: " + ErrorToString(errno));

  return IOStatus::OK();
}

int ZoneFsBackend::OpenZoneFile(uint64_t start, int flags) {
  std::string filename = LBAToZoneFile(start);
  return open(filename.c_str(), flags);
}

IOStatus ZoneFsBackend::Close(__attribute__((unused)) uint64_t start) {
  return IOStatus::OK();
}

int ZoneFsBackend::Read(char *buf, int size, uint64_t pos, bool direct) {
  int flags = direct ? O_RDONLY | O_DIRECT : O_RDONLY;
  uint64_t offset = LBAToZoneOffset(pos);
  int read_from_zone = std::min((uint64_t)size, zone_sz_ - offset);
  int read = 0;

  int fd = OpenZoneFile(pos, flags);
  if (fd == -1) return -1;

  while (read_from_zone) {
    int ret = pread(fd, buf, read_from_zone, offset);
    if (ret > 0) {
      read_from_zone -= ret;
      buf += ret;
      offset += ret;
      read += ret;
    } else {
      if (ret < 0) read = ret;
      break;
    }
  }
  close(fd);

  return read;
}

int ZoneFsBackend::Write(char *data, uint32_t size, uint64_t pos) {
  uint64_t offset = LBAToZoneOffset(pos);
  int write_to_zone = std::min((uint64_t)size, zone_sz_ - offset);
  int written = 0;

  if (readonly_) return -1;

  int fd = OpenZoneFile(pos, O_WRONLY | O_DIRECT);
  if (fd == -1) return -1;

  while (write_to_zone) {
    int ret = pwrite(fd, data, write_to_zone, offset);
    if (ret > 0) {
      write_to_zone -= ret;
      data += ret;
      offset += ret;
      written += ret;
    } else {
      if (ret < 0) written = ret;
      break;
    }
  }
  close(fd);

  return written;
}

bool ZoneFsBackend::ZoneIsSwr(__attribute__((unused))
                              std::unique_ptr<ZoneList> &zones,
                              __attribute__((unused)) unsigned int idx) {
  return true;
};

bool ZoneFsBackend::ZoneIsOffline(std::unique_ptr<ZoneList> &zones,
                                  unsigned int idx) {
  struct stat *z = &((struct stat *)zones->GetData())[idx];
  return ZENFS_ZONEFS_ZONE_OFFLINE(z->st_mode);
};
bool ZoneFsBackend::ZoneIsWritable(std::unique_ptr<ZoneList> &zones,
                                   unsigned int idx) {
  struct stat *z = &((struct stat *)zones->GetData())[idx];
  return (uint64_t)z->st_size < zone_sz_ &&
         (z->st_mode & (S_IWUSR | S_IWGRP | S_IWOTH));
};
bool ZoneFsBackend::ZoneIsActive(std::unique_ptr<ZoneList> &zones,
                                 unsigned int idx) {
  struct stat *z = &((struct stat *)zones->GetData())[idx];
  return z->st_size > 0 && (uint64_t)z->st_size < zone_sz_;
};
bool ZoneFsBackend::ZoneIsOpen(__attribute__((unused))
                               std::unique_ptr<ZoneList> &zones,
                               __attribute__((unused)) unsigned int idx) {
  // With zonefs there is no way to determine if a zone is open. Since the
  // the zone list is obtained before any ZenFS activity is performed, we
  // assume that all zones are closed.
  return false;
};
uint64_t ZoneFsBackend::ZoneStart(__attribute__((unused))
                                  std::unique_ptr<ZoneList> &zones,
                                  unsigned int idx) {
  return idx * zone_sz_;
};
uint64_t ZoneFsBackend::ZoneMaxCapacity(
    __attribute__((unused)) std::unique_ptr<ZoneList> &zones,
    __attribute__((unused)) unsigned int idx) {
  return zone_sz_;
};
uint64_t ZoneFsBackend::ZoneWp(std::unique_ptr<ZoneList> &zones,
                               unsigned int idx) {
  struct stat *z = &((struct stat *)zones->GetData())[idx];
  return idx * zone_sz_ + z->st_size;
};

}  // Namespace ROCKSDB_NAMESPACE

#endif  // !defined(ROCKSDB_LITE) && !defined(OS_WIN)
