#!/bin/bash

# Run and verify db_bench write workload and list the files in the FS

check_db_bench_workload_completion() {
  WORKLOAD=$1
  DBB_OUT=$2
  if [ $(grep -wc -E "$WORKLOAD\s+:" $DBB_OUT) -ne 1 ]; then
    echo "$(tput setaf 1)ERROR: the $WORKLOAD did not complete$(tput sgr 0)" 1>&2
    return -1
  fi
  return 0
}

utest_run_fillrandom_dbbench_workload() {
  DB_BENCH_PARAMS="--benchmarks=fillrandom --use_direct_io_for_flush_and_compaction --key_size=16 --value_size=800 --max_background_jobs=8 --num=1000000 --threads=2 --write_buffer_size=536870912 --target_file_size_base=1073741824 --target_file_size_multiplier=1 --max_bytes_for_level_multiplier=4 --histogram"
  DBB_OUT=$1

  $TOOLS_DIR/db_bench $DB_BENCH_PARAMS $FS_PARAMS > $DBB_OUT
  check_db_bench_workload_completion fillrandom $DBB_OUT
  return $?
}

DBB_LOG=$TEST_OUT-dbbench
utest_run_fillrandom_dbbench_workload $DBB_LOG
RES=$?
if [ $RES -ne 0 ]; then
  exit $RES
fi

$ZENFS_DIR/zenfs list --zbd=$ZDEV --path=rocksdbtest/dbbench > $TEST_OUT
RES=$?
if [ $RES -ne 0 ]; then
  exit $RES
fi

FILES=$(cat $TEST_OUT | wc -l)
if [ $FILES -lt 1 ]; then
  echo "No files reported by list" >> $TEST_OUT
  exit 1
fi

exit 0
