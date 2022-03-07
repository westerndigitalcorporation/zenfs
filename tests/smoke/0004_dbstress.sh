#!/bin/bash
source smoke/common.sh

OPS_PER_THREAD=1000
REOPENS=5

DB_STRESS_PARAMS="--ops_per_thread=$OPS_PER_THREAD --reopen=$REOPENS --db=dbname $FS_PARAMS"

echo "# Running db_stress with parameters: $DB_STRESS_PARAMS" > $TEST_OUT
$TOOLS_DIR/db_stress $DB_STRESS_PARAMS >> $TEST_OUT

if [ $(grep -wc "Verification successful" $TEST_OUT) -ne 1 ]; then
  echo "$(tput setaf 1)ERROR: db stress did not complete successfully$(tput sgr 0)"
  exit -1
fi

