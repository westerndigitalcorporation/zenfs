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
#include <mntent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>

#include "rocksdb/env.h"
#include "rocksdb/io_status.h"

#define ZENFS_ZONEFS_ZONE_OFFLINE(_f_mode) \
  (((_f_mode) &                            \
    (S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH)) == 0)
#define ZENFS_ZONEFS_DEFAULT_MAX_LIMIT 14
#define ZENFS_ZONEFS_DEFAULT_MAX_RD_LIMIT 100

namespace ROCKSDB_NAMESPACE {

ZoneFsFileCache::ZoneFsFileCache(int flags) {
  if (flags & O_RDONLY)
    max_ = ZENFS_ZONEFS_DEFAULT_MAX_RD_LIMIT;
  else
    max_ = ZENFS_ZONEFS_DEFAULT_MAX_LIMIT;
  flags_ = flags;
}

ZoneFsFileCache::~ZoneFsFileCache() {}

void ZoneFsFileCache::Put(uint64_t zone) {
  std::lock_guard<std::mutex> lock(mtx_);
  auto entry = map_.find(zone);
  if (entry != map_.end()) {
    list_.erase(entry->second);
    map_.erase(entry);
  }
}

void ZoneFsFileCache::Prune(unsigned limit) {
  while (list_.size() > limit) {
    map_.erase(list_.rbegin()->first);
    list_.pop_back();
  }
}

std::shared_ptr<ZoneFsFile> ZoneFsFileCache::Get(uint64_t zone,
                                                 std::string filename) {
  std::lock_guard<std::mutex> lock(mtx_);
  std::shared_ptr<ZoneFsFile> zoneFsFile(nullptr);
  auto entry = map_.find(zone);

  if (entry == map_.end()) {
    int fd = open(filename.c_str(), flags_);
    if (fd == -1) return nullptr;
    Prune(max_ - 1);
    zoneFsFile = std::make_shared<ZoneFsFile>(fd);
    list_.emplace_front(zone, zoneFsFile);
    map_.emplace(zone, list_.begin());
  } else {
    zoneFsFile = entry->second->second;
    list_.splice(list_.begin(), list_, entry->second);
  }
  return zoneFsFile;
}

void ZoneFsFileCache::Resize(unsigned new_size) {
  std::lock_guard<std::mutex> lock(mtx_);
  if (new_size < max_) {
    Prune(new_size);
  }
  max_ = new_size;
}

ZoneFsBackend::ZoneFsBackend(std::string mountpoint)
    : mountpoint_(mountpoint),
      zone_zero_fd_(-1),
      readonly_(false),
      rd_fds_(O_RDONLY),
      direct_rd_fds_(O_RDONLY | O_DIRECT),
      wr_fds_(O_WRONLY | O_DIRECT) {}

ZoneFsBackend::~ZoneFsBackend() {
  if (zone_zero_fd_ != -1) {
    // Releases zone 0 lock
    close(zone_zero_fd_);
  }
}

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

std::string ZoneFsBackend::GetBackingDevice(const char *mountpoint) {
  struct mntent *mnt = NULL;
  FILE *file = NULL;

  file = setmntent("/proc/mounts", "r");
  if (file == NULL) return "";

  while ((mnt = getmntent(file)) != NULL) {
    if (!strcmp(mnt->mnt_dir, mountpoint)) {
      std::string dev_name(mnt->mnt_fsname);
      std::size_t pos = dev_name.rfind("/");
      if (pos != std::string::npos) {
        dev_name.replace(0, pos + 1, "");
        return dev_name;
      }
    }
  }
  endmntent(file);

  return "";
}

unsigned int ZoneFsBackend::GetSysFsValue(std::string dev_name,
                                          std::string field) {
  std::ifstream sysfs;
  unsigned int val = ZENFS_ZONEFS_DEFAULT_MAX_LIMIT;

  sysfs.open("/sys/fs/zonefs/" + dev_name + "/" + field, std::ifstream::in);
  if (sysfs.is_open()) {
    sysfs >> val;
    sysfs.close();
  }

  return val;
}

IOStatus ZoneFsBackend::Open(bool readonly,
                             __attribute__((unused)) bool exclusive,
                             unsigned int *max_active_zones,
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
  int zone_zero_fd = open(seqdirname.c_str(), O_RDONLY);
  if (zone_zero_fd < 0) {
    return IOStatus::InvalidArgument(
        "Failed to open zonefs sequential zone 0: " + ErrorToString(errno));
  }

  if (flock(zone_zero_fd, LOCK_EX | LOCK_NB) == -1) {
    close(zone_zero_fd);
    return IOStatus::InvalidArgument(
        "Failed to lock zonefs sequential zone 0: " + ErrorToString(errno));
  }

  if (fstat(zone_zero_fd, &zonefs_stat) == -1) {
    close(zone_zero_fd);
    return IOStatus::InvalidArgument(
        "Failed to access zonefs sequential zone 0: " + ErrorToString(errno));
  }
  zone_sz_ = zonefs_stat.st_blocks * 512;
  block_sz_ = zonefs_stat.st_blksize;

  zone_zero_fd_ = zone_zero_fd;

  std::string backing_dev =
      ZoneFsBackend::GetBackingDevice(mountpoint_.c_str());
  if (backing_dev.length()) {
    *max_active_zones =
        ZoneFsBackend::GetSysFsValue(backing_dev, "max_active_seq_files");
    *max_open_zones =
        ZoneFsBackend::GetSysFsValue(backing_dev, "max_wro_seq_files");
  } else {
    *max_active_zones = *max_open_zones = ZENFS_ZONEFS_DEFAULT_MAX_LIMIT;
  }

  wr_fds_.Resize(*max_active_zones);

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
  PutZoneFile(start, O_WRONLY);

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
  PutZoneFile(start, O_WRONLY);

  return IOStatus::OK();
}

std::shared_ptr<ZoneFsFile> ZoneFsBackend::GetZoneFile(uint64_t start,
                                                       int flags) {
  std::shared_ptr<ZoneFsFile> zoneFsFile(nullptr);

  if (flags & O_WRONLY) {
    zoneFsFile = wr_fds_.Get(start / zone_sz_, LBAToZoneFile(start));
  } else if (flags & O_DIRECT) {
    zoneFsFile = direct_rd_fds_.Get(start / zone_sz_, LBAToZoneFile(start));
  } else {
    zoneFsFile = rd_fds_.Get(start / zone_sz_, LBAToZoneFile(start));
  }
  return zoneFsFile;
}

void ZoneFsBackend::PutZoneFile(uint64_t start, int flags) {
  uint64_t zone_nr = start / zone_sz_;

  if (flags & O_WRONLY) {
    wr_fds_.Put(zone_nr);
  }
}

IOStatus ZoneFsBackend::Close(uint64_t start) {
  PutZoneFile(start, O_WRONLY);
  return IOStatus::OK();
}

int ZoneFsBackend::InvalidateCache(uint64_t pos, uint64_t size) {
  uint64_t offset = LBAToZoneOffset(pos);

  std::shared_ptr<ZoneFsFile> file = GetZoneFile(pos, O_RDONLY);
  if (file == nullptr) return -EINVAL;

  return posix_fadvise(file->GetFd(), offset, size, POSIX_FADV_DONTNEED);
}

int ZoneFsBackend::Read(char *buf, int size, uint64_t pos, bool direct) {
  int flags = direct ? O_RDONLY | O_DIRECT : O_RDONLY;
  uint64_t offset = LBAToZoneOffset(pos);
  int read_from_zone = std::min((uint64_t)size, zone_sz_ - offset);
  int read = 0;

  std::shared_ptr<ZoneFsFile> file = GetZoneFile(pos, flags);
  if (file == nullptr) return -1;

  while (read_from_zone) {
    int ret = pread(file->GetFd(), buf, read_from_zone, offset);
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

  return read;
}

int ZoneFsBackend::Write(char *data, uint32_t size, uint64_t pos) {
  uint64_t offset = LBAToZoneOffset(pos);
  int write_to_zone = std::min((uint64_t)size, zone_sz_ - offset);
  int written = 0;

  if (readonly_) return -1;

  std::shared_ptr<ZoneFsFile> file = GetZoneFile(pos, O_WRONLY | O_DIRECT);
  if (file == nullptr) return -1;

  while (write_to_zone) {
    int ret = pwrite(file->GetFd(), data, write_to_zone, offset);
    if (ret > 0) {
      write_to_zone -= ret;
      data += ret;
      offset += ret;
      written += ret;
      if (offset == zone_sz_) PutZoneFile(pos, O_WRONLY);
    } else {
      if (ret < 0) written = ret;
      break;
    }
  }

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
