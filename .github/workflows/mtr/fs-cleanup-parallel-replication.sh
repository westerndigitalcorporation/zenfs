#!/bin/bash
set -e

WORKER=$1
AUX1=$((WORKER*2-2))
AUX2=$((AUX1+1))
SOURCE_VARNAME=SOURCE_FS_URI$WORKER
REPLICA_VARNAME=REPLICA_FS_URI$WORKER
SOURCE_FS_URI=${!SOURCE_VARNAME}
REPLICA_FS_URI=${!REPLICA_VARNAME}
SOURCE_ZBD=${SOURCE_FS_URI#zenfs://dev:}
REPLICA_ZBD=${REPLICA_FS_URI#zenfs://dev:}

echo reinitializing ${SOURCE_ZBD} \(source\) and ${REPLICA_ZBD} \(replica\) for worker $1

rm -fr /tmp/zenfs_aux_$AUX1
rm -fr /tmp/zenfs_aux_$AUX2
zenfs mkfs --zbd=$SOURCE_ZBD --aux-path=/tmp/zenfs_aux_$AUX1 --force
zenfs mkfs --zbd=$REPLICA_ZBD --aux-path=/tmp/zenfs_aux_$AUX2 --force
