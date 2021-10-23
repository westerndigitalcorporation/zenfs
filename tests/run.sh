#!/bin/bash
set -e

NAME=$1
TEST_DIR=$2
TESTS=$(ls $TEST_DIR/*_*.sh)

OK_TESTS=0
FAILED_TESTS=0

export TOOLS_DIR="../../../"
export ZENFS_DIR="../util/"

RESULT_PATH="results/$NAME"
RESULT_DIR="results/$NAME/$TEST_DIR"

mkdir -p $RESULT_DIR
rm -rf $RESULT_DIR/*

for TEST in $TESTS
do
  TESTCASE="$TEST"
  echo "$(tput setaf 3)Running $TEST $(tput sgr 0)"
  
  export RESULT_DIR="$RESULT_DIR"
  export TEST_OUT="$RESULT_PATH/${TEST/sh/out}"
  TEST_RES=$($TESTCASE)
  RES=$?
  echo ""
  if [ $RES -eq 0 ]; then
    echo "$(tput setaf 2)OK$(tput sgr 0)"
    OK_TESTS=$((OK_TESTS+1))
  else
    echo "$(tput setaf 1)FAILED$(tput sgr 0)"
    FAILED_TESTS=$((FAILED_TESTS+1))
  fi
done

echo ""

if [ $FAILED_TESTS -eq 0 ]; then
  echo "$(tput setaf 2)ALL TESTS PASSED$(tput sgr 0)"
else
  echo "$(tput setaf 1)$FAILED_TESTS TESTS FAILED$(tput sgr 0)"
fi

echo "Test output avaiable at $RESULT_DIR"

exit $FAILED_TESTS
