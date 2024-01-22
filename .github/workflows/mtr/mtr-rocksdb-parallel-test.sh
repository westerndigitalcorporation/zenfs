#!/bin/bash
set -e

/scripts/fs-multidev-cleanup.sh 3

cd $INSTALL_PATH

FS_URI1=zenfs://dev:$DEVICE0 FS_URI2=zenfs://dev:$DEVICE1 \
       FS_URI3=zenfs://dev:$DEVICE2 FS_URI4=zenfs://dev:$DEVICE3  \
       ./mtr --suite=rocksdb \
       --skip-rpl \
       --skip-test-list /scripts/expected-mtr-failures \
       --defaults-extra-file=/scripts/zenfs_parallel.cnf \
       --fs-cleanup-hook="@/scripts/fs-cleanup-parallel.sh" \
       --parallel=4
