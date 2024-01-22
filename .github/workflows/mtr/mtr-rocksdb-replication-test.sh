#!/bin/bash
set -e

/scripts/fs-cleanup-replication.sh

cd $INSTALL_PATH

SOURCE_FS_URI1=zenfs://dev:$DEVICE0 \
              REPLICA_FS_URI1=zenfs://dev:$DEVICE1 \
              ./mtr --suite=rocksdb \
              --skip-test-list /scripts/expected-mtr-failures \
              --defaults-extra-file=/scripts/zenfs_rpl.cnf \
              --fs-cleanup-hook="@/scripts/fs-cleanup-replication.sh"
