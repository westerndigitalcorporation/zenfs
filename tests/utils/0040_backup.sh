#!/bin/bash

# Do a zenfs backup and verify the backed up data and ondisk data match.

source utils/common.sh

BACKUP_DIR=$RESULT_DIR/backup

LIST=$($ZENFS_DIR/zenfs list --zbd=$ZDEV --path=rocksdbtest/dbbench | wc -l)
echo "List reported $LIST files, backup dir is $BACKUP_DIR"
mkdir -p $BACKUP_DIR
$ZENFS_DIR/zenfs backup --zbd=$ZDEV --path=$BACKUP_DIR >> $TEST_OUT
RES=$?
if [ $RES -ne 0 ]; then
  echo "Backup failed"
  exit $RES
fi

FILECOUNT=$(ls -1 $BACKUP_DIR/rocksdbtest/dbbench | wc -l)
if [ $LIST -ne $FILECOUNT ]; then
  echo "Files mismatch after backup" >> $TEST_OUT
  exit 1
fi

utest_run_ldb_verify $BACKUP_DIR
RES=$?
if [ $RES -ne 0 ]; then
  echo "Failed to verify backed up files" >> $TEST_OUT
  exit $RES
fi

rm $RESULT_DIR/ondisk_db_dump
rm $RESULT_DIR/backup_db_dump

exit 0
