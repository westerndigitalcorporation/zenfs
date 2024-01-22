#!/bin/bash
set -e

WORKER=$1
WORKER=$((WORKER-1))
DEV="DEVICE$WORKER"
echo "Reinitializing $DEV"
if [ -z "${!DEV}" ]
then
    echo "$DEV must be set for zenfs-mtr"
    exit 1
fi

rm -fr /tmp/zenfs_aux_$WORKER
zenfs mkfs --zbd=${!DEV} --aux-path=/tmp/zenfs_aux_$WORKER --force
