#!/bin/bash

# Format a ZBD and create a ZenFS filesystem

rm -rf /tmp/zenfs-aux
$ZENFS_DIR/zenfs mkfs --zbd=$ZDEV --aux-path=/tmp/zenfs-aux --force --finish-threshold=5 > $TEST_OUT
RES=$?
if [ $RES -ne 0 ]; then
  exit $RES
fi

exit 0
