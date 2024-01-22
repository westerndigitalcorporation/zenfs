#!/bin/bash
set -e

for i in $(seq 0 3)
do
    DEV="DEVICE$i"
    if [ -n "${!DEV}" ]
    then
        chown mysql:mysql /dev/${!DEV}
    fi
done

sudo --preserve-env=LD_LIBRARY_PATH,PATH,PERCONA_SRC,PERCONA_BUILD,PERCONA_INSTALL,DEVICE,DEVICE0,DEVICE1,DEVICE2,DEVICE3,DEVICE4,DEVICE5,INSTALL_PATH $@
