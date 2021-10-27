#!/bin/bash

CRASHTEST_SRC_DIR="$(dirname $(readlink -f $0))"

$CRASHTEST_SRC_DIR/base-crashtest-call.sh whitebox 
RES=$?
if [ $RES -ne 0 ]; then
  exit $RES
fi

$CRASHTEST_SRC_DIR/base-crashtest-call.sh whitebox --simple 
RES=$?
if [ $RES -ne 0 ]; then
  exit $RES
fi

exit 0
