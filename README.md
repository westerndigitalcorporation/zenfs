# ZenFS

ZenFS is a file system plugin that utilizes RockDB's FileSystem interface to
place files into zones on a raw zoned block device. By separating files into
zones and utilizing write life time hints to co-locate data of similar life
times the system write amplification is greatly reduced compared to
conventional block devices. ZenFS ensures that there is no background
garbage collection in the file system or on the disk, improving performance
in terms of throughput, tail latencies and disk endurance.

## Dependencies

ZenFS depends on[ libzbd ](https://github.com/westerndigitalcorporation/libzbd)
and Linux kernel 5.4 or later to perform zone management operations. To use
ZenFS on SSDs with Zoned Namespaces kernel 5.9 or later is required.

# Getting started

## Build

Download, build and install libzbd. See the libzbd [ README ](https://github.com/westerndigitalcorporation/libzbd/blob/master/README.md) 
for instructions.

Download rockksdb and the zenfs projects:
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

## Testing with db_bench

To instruct db_bench to use zenfs on a specific zoned block device, the --fs_uri parameter is used.
The device name may be used by specifying `--fs_uri=zenfs://dev:<zoned block device name>` or by
specifying a unique identifier for the created file system by specifying `--fs_uri=zenfs://uuid:<UUID>`.
UUIDs can be listed using `./plugin/zenfs/util/zenfs ls-uuid`

```
./db_bench --fs_uri=zenfs://dev:<zoned block device name> --benchmarks=fillrandom --use_direct_io_for_flush_and_compaction

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

* Extents are block-aligned, continious regions on the block device
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
