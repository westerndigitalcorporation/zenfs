#!/bin/bash
set -e

# Example:
#   sudo ./zenfs_base_performance.sh nvme2n1

DEV=$1

echo deadline > /sys/class/block/$DEV/queue/scheduler

GOOD_PARAMS=$(sudo ./get_good_db_bench_params_for_zenfs.sh $DEV)

AUXPATH=$DEV-aux
sudo rm -rf /tmp/$AUXPATH && sudo ../util/zenfs mkfs --zbd=$DEV --aux_path=/tmp/$AUXPATH --finish_threshold=10 --force

FS_URI="zenfs://dev:$DEV"

NAME="zenfs-$DEV-baseline_performance"
echo "$(tput setaf 4)Running ZenFS baseline performance tests, results will be stored in results/$NAME $(tput sgr 0)"

DB_BENCH_EXTRA_PARAMS="$GOOD_PARAMS" FS_PARAMS="--fs_uri=$FS_URI" ./run.sh $NAME quick_performance
DB_BENCH_EXTRA_PARAMS="$GOOD_PARAMS" FS_PARAMS="--fs_uri=$FS_URI" ./run.sh $NAME long_performance
