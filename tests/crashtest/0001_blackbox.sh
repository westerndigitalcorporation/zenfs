#!/bin/bash

CRASHTEST_SRC_DIR="$(dirname $(readlink -f $0))"

$CRASHTEST_SRC_DIR/base-crashtest-call.sh blackbox --simple
RES=$?
if [ $RES -ne 0 ]; then
  exit $RES
fi

$CRASHTEST_SRC_DIR/base-crashtest-call.sh blackbox --test_best_efforts_recovery
RES=$?
if [ $RES -ne 0 ]; then
  exit $RES
fi

exit 0
