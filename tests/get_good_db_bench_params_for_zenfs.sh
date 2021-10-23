#!/bin/bash
# This script intends to generate a good set of parameters for dbbench to use as a base for zenfs testing

set -e

DEV=$1
CAP_SECTORS=$(blkzone report -c 5 /dev/$DEV | grep -oP '(?<=cap )[0-9xa-f]+' | head -1)
ZONE_CAP=$(($CAP_SECTORS * 512))
WB_SIZE=$(( 2 * 1024 * 1024 * 1024))
echo "--target_file_size_base=$(($ZONE_CAP * 2 * 95 / 100)) --use_direct_io_for_flush_and_compaction --max_bytes_for_level_multiplier=4 --max_background_jobs=8 --use_direct_reads --write_buffer_size=$WB_SIZE"

