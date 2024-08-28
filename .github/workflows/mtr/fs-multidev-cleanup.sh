#!/bin/bash
set -e

if [ -z "${1}" ]
then
    echo "Usage: $0 LAST_DEVICE_INDEX"
    exit 1
fi

for i in $(seq 0 $1)
do
    DEV="DEVICE$i"
    echo "Reinitializing $DEV"
    if [ -z "${!DEV}" ]
    then
        echo "$DEV must be set for zenfs-mtr"
        exit 1
    fi

    rm -fr /tmp/zenfs_aux_$i
    zenfs mkfs --zbd=${!DEV} --aux-path=/tmp/zenfs_aux_$i --force
done
