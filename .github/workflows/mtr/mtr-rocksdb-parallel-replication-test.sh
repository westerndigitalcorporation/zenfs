#!/bin/bash
set -e

/scripts/fs-multidev-cleanup.sh 3

cd $INSTALL_PATH

SOURCE_FS_URI1=zenfs://dev:$DEVICE0 SOURCE_FS_URI2=zenfs://dev:$DEVICE2 \
              REPLICA_FS_URI1=zenfs://dev:$DEVICE1 REPLICA_FS_URI2=zenfs://dev:$DEVICE3 \
              ./mtr --suite=rocksdb \
              --skip-test-list /scripts/expected-mtr-failures \
              --defaults-extra-file=/scripts/zenfs_rpl.cnf \
              --fs-cleanup-hook="@/scripts/fs-cleanup-parallel-replication.sh" \
              --parallel=2
