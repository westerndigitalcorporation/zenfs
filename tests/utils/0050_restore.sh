#!/bin/bash

# Do a zenfs restore and verify the restored data ondisk and source data match.

source utils/common.sh

BACKUP_DIR=$RESULT_DIR/backup

cp $BACKUP_DIR/write_lifetime_hints.dat $BACKUP_DIR/rocksdbtest/dbbench/
$ZENFS_DIR/zenfs restore --zbd=$ZDEV --path=$BACKUP_DIR/rocksdbtest/dbbench/ --restore-path=rocksdbtest/dbbench >> $TEST_OUT
RES=$?
if [ $RES -ne 0 ]; then
  echo "Restore failed" >> $TEST_OUT
  exit $RES
fi

utest_run_ldb_verify $BACKUP_DIR
RES=$?
if [ $RES -ne 0 ]; then
  echo "Failed to verify restored up files" >> $TEST_OUT
  exit $RES
fi

rm $RESULT_DIR/ondisk_db_dump
rm $RESULT_DIR/backup_db_dump
rm -rf $BACKUP_DIR/
exit 0
