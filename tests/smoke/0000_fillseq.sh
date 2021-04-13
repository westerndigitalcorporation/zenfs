#!/bin/bash
source smoke/common.sh

DB_BENCH_PARAMS="--benchmarks=fillseq --num=$NUM --value_size=$VALUE_SIZE --histogram $FS_PARAMS $DB_BENCH_EXTRA_PARAMS"

echo "# Running db_bench with parameters: $DB_BENCH_PARAMS" > $TEST_OUT
$TOOLS_DIR/db_bench $DB_BENCH_PARAMS >> $TEST_OUT

check_db_bench_workload_completion fillseq
exit $?
