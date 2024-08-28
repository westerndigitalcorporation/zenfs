#!/bin/bash
set -e

if [ -z "${DEVICE0}" ]
then
    echo "DEVICE0 not set for the test script"
    exit 1
fi

rm -fr /tmp/zenfs_aux
zenfs mkfs --zbd=$DEVICE0 --aux-path=/tmp/zenfs_aux --force

cd $INSTALL_PATH

./mtr --suite=rocksdb \
      --skip-rpl \
      --skip-test-list /scripts/expected-mtr-failures \
      --mysqld=--loose-rocksdb-fs-uri=zenfs://dev:$DEVICE0 \
      --fs-cleanup-hook="rm -rf /tmp/zenfs_aux; zenfs mkfs --zbd=$DEVICE0 --aux_path=/tmp/zenfs_aux --force"
