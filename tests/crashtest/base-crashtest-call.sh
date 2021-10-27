#!/bin/bash

if [[ -z "${STRESS_CMD}" ]] || [[ ! -f "${STRESS_CMD}" ]]; then
  echo "STRESS_CMD must supply the binary for db_stress"
  exit 1
fi

if [[ -z "${FS_PARAMS}" ]]; then
  echo "FS_PARAMS should supply e.g. a file system uri like: '--fs_uri=zenfs://dev:nvme0n1'"
  exit 1
fi

TEST_TYPE=$1

if [[ -z "${TEST_TYPE}" ]]; then
  echo "$0 must be called with a test type (whitebox or blackbox) as parameter. Usage:"
  echo "$0 whitebox|backbox [ADDITIONAL_PARAM] ..."
  exit 1
fi

CRASHTEST_SRC_DIR="$(dirname $(readlink -f $0))"
ADDITIONAL_PARAMS="${@: 2}"
ADDITIONAL_PARAMS="${ADDITIONAL_PARAMS} ${CRASHTEST_EXTRA_PARAMS}"

set -o pipefail
python3 -u $CRASHTEST_SRC_DIR/db_crashtest.py $TEST_TYPE --stress_cmd=$STRESS_CMD $FS_PARAMS $ADDITIONAL_PARAMS | tee $TEST_OUT
RES=$?
set +o pipefail

if [ $RES -ne 0 ]; then
  exit $RES
fi

exit 0
