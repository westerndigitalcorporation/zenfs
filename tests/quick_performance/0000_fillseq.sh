#!/bin/bash

WORKLOAD_SZ=10000000000

echo "" > $TEST_OUT

for VALUE_SIZE in 100 200 400 1000 2000 8000; do
  NUM=$(( $WORKLOAD_SZ / $VALUE_SIZE ))
  DB_BENCH_PARAMS="--benchmarks=fillseq --num=$NUM --value_size=$VALUE_SIZE --compression_type=none --histogram $FS_PARAMS $DB_BENCH_EXTRA_PARAMS"
  echo "# Running db_bench with parameters: $DB_BENCH_PARAMS" >> $TEST_OUT
  $TOOLS_DIR/db_bench $DB_BENCH_PARAMS >> $TEST_OUT
  RES=$?
  if [ $RES -ne 0 ]; then
    exit $RES
  fi
done

exit 0
