# Exit on any error
set -e
# Helper(s)

utest_run_ldb_verify() {
  ONDISK_DB="--db=rocksdbtest/dbbench"
  BACKUP_DB="--db=$1/rocksdbtest/dbbench"

  echo "ONDISK_DB=$ONDISK_DB , BACKUP_DBPATH=$BACKUP_DB, LDB_PARAMS=$LDB_PARAMS" >> $RESULT_DIR/log
  $TOOLS_DIR/ldb dump --hex $FS_PARAMS $ONDISK_DB > $RESULT_DIR/ondisk_db_dump

  $TOOLS_DIR/ldb dump --hex $BACKUP_DB > $RESULT_DIR/backup_db_dump

  diff $RESULT_DIR/ondisk_db_dump $RESULT_DIR/backup_db_dump

  return $?
}
