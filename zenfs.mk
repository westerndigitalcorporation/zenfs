
zenfs_SOURCES-y = \
	fs/fs_zenfs.cc \
	fs/zbd_zenfs.cc \
	fs/io_zenfs.cc \
	fs/zonefs_zenfs.cc \
	fs/zbdlib_zenfs.cc

zenfs_HEADERS-y = \
	fs/fs_zenfs.h \
	fs/zbd_zenfs.h \
	fs/io_zenfs.h \
	fs/version.h \
	fs/metrics.h \
	fs/snapshot.h \
	fs/filesystem_utility.h \
	fs/zonefs_zenfs.h \
	fs/zbdlib_zenfs.h

zenfs_PKGCONFIG_REQUIRES-y += "libzbd >= 1.5.0"

ZENFS_EXPORT_PROMETHEUS ?= n
zenfs_HEADERS-$(ZENFS_EXPORT_PROMETHEUS) += fs/metrics_prometheus.h
zenfs_SOURCES-$(ZENFS_EXPORT_PROMETHEUS) += fs/metrics_prometheus.cc
zenfs_CXXFLAGS-$(ZENFS_EXPORT_PROMETHEUS) += -DZENFS_EXPORT_PROMETHEUS=1
zenfs_PKGCONFIG_REQUIRES-$(ZENFS_EXPORT_PROMETHEUS) += ", prometheus-cpp-pull == 1.1.0"

zenfs_SOURCES += $(zenfs_SOURCES-y)
zenfs_HEADERS += $(zenfs_HEADERS-y)
zenfs_CXXFLAGS += $(zenfs_CXXFLAGS-y)
zenfs_LDFLAGS += -u zenfs_filesystem_reg

ZENFS_ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

$(shell cd $(ZENFS_ROOT_DIR) && ./generate-version.sh)
ifneq ($(.SHELLSTATUS),0)
$(error Generating ZenFS version failed)
endif

zenfs_PKGCONFIG_REQUIRES = $(zenfs_PKGCONFIG_REQUIRES-y)
