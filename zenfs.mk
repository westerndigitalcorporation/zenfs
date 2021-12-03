zenfs_SOURCES = fs/fs_zenfs.cc fs/zbd_zenfs.cc fs/io_zenfs.cc
zenfs_HEADERS = fs/fs_zenfs.h fs/zbd_zenfs.h fs/io_zenfs.h fs/metrics.h fs/metrics_sample.h fs/snapshot.h
zenfs_LDFLAGS = -lzbd -u zenfs_filesystem_reg
