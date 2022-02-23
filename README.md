# ZenFS: RocksDB Storage Backend for ZNS SSDs and SMR HDDs

ZenFS is a file system plugin that utilizes [RocksDB](https://github.com/facebook/rocksdb)'s FileSystem interface to
place files into zones on a raw zoned block device. By separating files into
zones and utilizing write life time hints to co-locate data of similar life
times the system write amplification is greatly reduced compared to
conventional block devices. ZenFS ensures that there is no background
garbage collection in the file system or on the disk, improving performance
in terms of throughput, tail latencies and disk endurance.

## Community
For help or questions about zenfs usage (e.g. "how do I do X?") see below, on join us on [Matrix](https://app.element.io/#/room/#zonedstorage-general:matrix.org), or on [Slack](https://join.slack.com/t/zonedstorage/shared_invite/zt-uyfut5xe-nKajp9YRnEWqiD4X6RkTFw).

To report a bug, file a documentation issue, or submit a feature request, please open a GitHub issue.

For release announcements and other discussions, please subscribe to this repository or join us on Matrix or Slack.

## Dependencies

ZenFS depends on[ libzbd ](https://github.com/westerndigitalcorporation/libzbd)
and Linux kernel 5.4 or later to perform zone management operations. To use
ZenFS on SSDs with Zoned Namespaces, Linux kernel 5.9 or later is required.
ZenFS works with RocksDB version v6.19.3 or later.

# Getting started

## Build

Download, build and install libzbd. See the libzbd [ README ](https://github.com/westerndigitalcorporation/libzbd/blob/master/README.md) 
for instructions.

Download rocksdb and the zenfs projects:
```
$ git clone https://github.com/facebook/rocksdb.git
$ cd rocksdb
$ git clone https://github.com/westerndigitalcorporation/zenfs plugin/zenfs
```

Build and install rocksdb with zenfs enabled:
```
$ DEBUG_LEVEL=0 ROCKSDB_PLUGINS=zenfs make -j48 db_bench install
```

Build the zenfs utility:
```
$ pushd
$ cd plugin/zenfs/util
$ make
$ popd
```

## Configure the IO Scheduler for the zoned block device

The IO scheduler must be set to deadline to avoid writes from being reordered.
This must be done every time the zoned name space is enumerated (e.g at boot).

```
echo deadline > /sys/class/block/<zoned block device>/queue/scheduler
```

## Creating a ZenFS file system

Before ZenFS can be used in RocksDB, the file system metadata and superblock must be set up.
This is done with the zenfs utility, using the mkfs command:

```
./plugin/zenfs/util/zenfs mkfs --zbd=<zoned block device> --aux_path=<path to store LOG and LOCK files>
```

## ZenFS on-disk file formats

ZenFS Version 1.0.0 and earlier uses version 1 of the on-disk format.
ZenFS Version 2.0.0 introduces breaking on-disk-format changes (inline extents, support for zones larged than 4GB).

To migrate between different versions of the on-disk file format, use the zenfs backup/restore commands.

```
# Backup the disk contents to the host file system using the version of zenfs that was used to store the current database
./plugin/zenfs/util/zenfs backup --path=<path to store backup> --zbd=<zoned block device>

# Switch to the new version of ZenFS you want to use (e.g 1.0.2 -> 2.0.0), rebuild and create a new file system
# Remove the current aux folder if needed.
./plugin/zenfs/util/zenfs mkfs --force --zbd=<zoned block device> --aux_path=<path to store LOG and LOCK files>

# Restore the database files to the new version of the file system
./plugin/zenfs/util/zenfs restore --path=<path to backup> --zbd=<zoned block device>

```

## Testing with db_bench

To instruct db_bench to use zenfs on a specific zoned block device, the --fs_uri parameter is used.
The device name may be used by specifying `--fs_uri=zenfs://dev:<zoned block device name>` or by
specifying a unique identifier for the created file system by specifying `--fs_uri=zenfs://uuid:<UUID>`.
UUIDs can be listed using `./plugin/zenfs/util/zenfs ls-uuid`

```
./db_bench --fs_uri=zenfs://dev:<zoned block device name> --benchmarks=fillrandom --use_direct_io_for_flush_and_compaction

```

## Performance testing

If you want to use db_bench for testing zenfs performance, there is a a convenience script
that runs the 'long' and 'quick' performance test sets with a good set of parameters
for the drive.

`cd tests; ./zenfs_base_performance.sh <zoned block device name>`


## Crashtesting
To run the crashtesting scripts, Python3 is required.
Crashtesting is done through the modified db_crashtest.py
(original [db_crashtest.py](https://github.com/facebook/rocksdb/blob/main/tools/db_crashtest.py)).
It kills the DB at a random point in time (blackbox) or at predefined places
in the RocksDB code (whitebox) and checks for recovery.
For further reading visit the RocksDB [wiki](https://github.com/facebook/rocksdb/wiki/Stress-test).
However the goal for ZenFS crashtesting is to cover a specified set of
parameters rather than randomized continuous testing.

The convenience script can be used to run all crashtest sets defined in `tests/crashtest`.
```
cd tests; ./zenfs_base_crashtest.sh <zoned block device name>
```

# ZenFS Internals

## Architecture overview
![zenfs stack](https://user-images.githubusercontent.com/447288/84152469-fa3d6300-aa64-11ea-87c4-8a6653bb9d22.png)

ZenFS implements the FileSystem API, and stores all data files on to a raw 
zoned block device. Log and lock files are stored on the default file system
under a configurable directory. Zone management is done through libzbd and
ZenFS io is done through normal pread/pwrite calls.

## File system implementation

Files are mapped into into a set of extents:

* Extents are block-aligned, continuous regions on the block device
* Extents do not span across zones
* A zone may contain more than one extent
* Extents from different files may share zones

### Reclaim 

ZenFS is exceptionally lazy at current state of implementation and does 
not do any garbage collection whatsoever. As files gets deleted, the used
capacity zone counters drops and when it reaches zero, a zone can be reset
and reused.

###  Metadata 

Metadata is stored in a rolling log in the first zones of the block device.

Each valid meta data zone contains:

* A superblock with the current sequence number and global file system metadata
* At least one snapshot of all files in the file system
* Incremental file system updates (new files, new extents, deletes, renames etc)

# Contribution Guide

ZenFS uses clang-format with Google code style. You may run the following commands
before submitting a PR.

```bash
clang-format-11 -n -Werror --style=file fs/* util/zenfs.cc # Check for style issues
clang-format-11 -i --style=file fs/* util/zenfs.cc         # Auto-fix the style issues
```
