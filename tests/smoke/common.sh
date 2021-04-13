# Exit on any error
set -e

# Common smoke test settings
NUM=1000000
VALUE_SIZE=800

# Helper(s)

check_db_bench_workload_completion() {
  WORKLOAD=$1
  if [ $(grep -wc -E "$WORKLOAD\s+:" $TEST_OUT) -ne 1 ]; then
    echo "$(tput setaf 1)ERROR: the $WORKLOAD did not complete$(tput sgr 0)" 1>&2
    return -1
  fi
  return 0
}

