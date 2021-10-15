#!/bin/bash

WORKLOAD_SZ=1000000000

echo "" > $TEST_OUT

for VALUE_SIZE in 1000 2000 4000; do
  NUM=$(( $WORKLOAD_SZ / $VALUE_SIZE ))
  DB_BENCH_PARAMS="--benchmarks=fillrandom --num=$NUM --value_size=$VALUE_SIZE --compression_type=none --sync=1 --histogram $FS_PARAMS $DB_BENCH_EXTRA_PARAMS"
  echo "# Running db_bench with parameters: $DB_BENCH_PARAMS" >> $TEST_OUT
  $TOOLS_DIR/db_bench $DB_BENCH_PARAMS >> $TEST_OUT
  RES=$?
  if [ $RES -ne 0 ]; then
    exit $RES
  fi
done

exit 0
