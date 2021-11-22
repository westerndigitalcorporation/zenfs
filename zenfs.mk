zenfs_SOURCES = fs/fs_zenfs.cc fs/zbd_zenfs.cc fs/io_zenfs.cc
zenfs_HEADERS = fs/fs_zenfs.h fs/zbd_zenfs.h fs/io_zenfs.h fs/version.h fs/metrics.h fs/snapshot.h
zenfs_LDFLAGS = -u zenfs_filesystem_reg

ZENFS_ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

$(shell cd $(ZENFS_ROOT_DIR) && ./generate-version.sh)
ifneq ($(.SHELLSTATUS),0)
$(error Generating ZenFS version failed)
endif

zenfs_PKGCONFIG_REQUIRES = "libzbd >= 1.5.0"
