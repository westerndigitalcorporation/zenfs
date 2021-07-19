# ZenFS Utilities

This directory contains the ZenFS command line utility.

## Usage

TODO

## ZenFS Dump Analysis Tool

When ZenFS gets full, users may need to quickly format or recycle the disk,
while analyzing why it becomes full later in offline environment. Therefore,
we added a `dump` command to ZenFS tools to export current file system status
in JSON format. At the same time, we write a simple script in Python to
analyze the file system in offline environment.

### Quick Start

First of all, we need to dump metadata from ZenFS.

```bash
./zenfs dump --zbd=nvme3n2 > result.json
```

Pro tips: the json file might be too large for transfer. We could gzip it for
file transfer.

```bash
./zenfs dump --zbd=nvme3n2 | gzip > result.json.gz
```

Then, we could analyze the json file. Python 3.9+ is required, and we need to
install some packages.

```bash
pip3 install hurry.filesize cysimdjson cython tqdm -U
./zen-dump.py extents
```

### Usage

* `./zen-dump.py zones` reports status of each zone.
* `./zen-dump.py files` reports status of each file.
* `./zen-dump.py extents` visualizes what extent is in each block.

#### Zone Report

`./zen-dump.py zones` will report the following information:

```plain
--- META ZONES ---
Zone #0: start=0x000000000000 wp=0x000000000000 remaining_capacity=0x43500000(1G) max_capacity=0x43500000(1G) used_capacity=0x0(0B) lifetime=0
Zone #1: start=0x000080000000 wp=0x00009c531000 remaining_capacity=0x26fcf000(623M) max_capacity=0x43500000(1G) used_capacity=0x0(0B) lifetime=0
Zone #2: start=0x000100000000 wp=0x000100000000 remaining_capacity=0x43500000(1G) max_capacity=0x43500000(1G) used_capacity=0x0(0B) lifetime=0
--- IO ZONES ---
Zone #0: start=0x000180000000 wp=0x000180316000 remaining_capacity=0x431ea000(1G) max_capacity=0x43500000(1G) used_capacity=0xafaf6(702K) lifetime=0
Zone #1: start=0x000200000000 wp=0x000243500000 remaining_capacity=0x0(0B) max_capacity=0x43500000(1G) used_capacity=0xf314087(243M) lifetime=0
Zone #2: start=0x000280000000 wp=0x0002c3500000 remaining_capacity=0x0(0B) max_capacity=0x43500000(1G) used_capacity=0xf3186fc(243M) lifetime=0
Zone #3: start=0x000300000000 wp=0x000343500000 remaining_capacity=0x0(0B) max_capacity=0x43500000(1G) used_capacity=0xf335d9e(243M) lifetime=0
Zone #4: start=0x000380000000 wp=0x0003c3500000 remaining_capacity=0x0(0B) max_capacity=0x43500000(1G) used_capacity=0x80df757(128M) lifetime=0
Zone #5: start=0x000400000000 wp=0x000443500000 remaining_capacity=0x0(0B) max_capacity=0x43500000(1G) used_capacity=0xf3495f1(243M) lifetime=0
Zone #6: start=0x000480000000 wp=0x0004c3500000 remaining_capacity=0x0(0B) max_capacity=0x43500000(1G) used_capacity=0x400a6ea7(1G) lifetime=0
......
total=0xedb2800000(950G) free=0x1d0b64000(7G) used=0x34e5f06739(211G) reclaimable=0xb8cc8f98c7(739G)
```

#### File Report

`./zen-dump.py files` will report the following information:

```plain
--- FILES ---
#3 test_db/IDENTITY: size=0x25(37B) hint=0 len(extents)=1
  Zone #0: len(extents)=1 size=0x25(37B)
#4 test_db/000003.log: size=0xf314087(243M) hint=2 len(extents)=6433
  Zone #1: len(extents)=6433 size=0xf314087(243M)
#5 test_db/OPTIONS-000005: size=0x14d6(5K) hint=0 len(extents)=1
  Zone #0: len(extents)=1 size=0x14d6(5K)
#6 test_db/000006.log: size=0xf3186fc(243M) hint=2 len(extents)=7164
  Zone #2: len(extents)=7164 size=0xf3186fc(243M)
#7 test_db/000007.log: size=0xf3495f1(243M) hint=2 len(extents)=7906
  Zone #5: len(extents)=7906 size=0xf3495f1(243M)
#11 test_db/MANIFEST-000011: size=0xae5fb(697K) hint=0 len(extents)=760
  Zone #0: len(extents)=760 size=0xae5fb(697K)
#12 test_db/CURRENT: size=0x10(16B) hint=0 len(extents)=1
  Zone #6: len(extents)=1 size=0x10(16B)
#13 test_db/000012.log: size=0xf31f856(243M) hint=2 len(extents)=8473
  Zone #9: len(extents)=8473 size=0xf31f856(243M)
#42 test_db/000041.log: size=0xf337023(243M) hint=2 len(extents)=15497
  Zone #27: len(extents)=15497 size=0xf337023(243M)
#76 test_db/000075.sst: size=0x80dfa77(128M) hint=3 len(extents)=1
  Zone #41: len(extents)=1 size=0x80dfa77(128M)
#82 test_db/000081.log: size=0xf33d234(243M) hint=2 len(extents)=16818
  Zone #39: len(extents)=16818 size=0xf33d234(243M)
#83 test_db/000082.sst: size=0x80e0dd1(128M) hint=3 len(extents)=1
  Zone #47: len(extents)=1 size=0x80e0dd1(128M)
#86 test_db/000085.sst: size=0x80e0d25(128M) hint=3 len(extents)=1
  Zone #49: len(extents)=1 size=0x80e0d25(128M)
#88 test_db/000087.sst: size=0x80e0d9e(128M) hint=3 len(extents)=1
  Zone #51: len(extents)=1 size=0x80e0d9e(128M)
```

#### Extent Report

`./zen-dump.py extents` will report the following information:

```plain
Zone #27: start=0x000f00000000 wp=0x000f43500000 remaining_capacity=0x0(0B) max_capacity=0x43500000(1G) used_capacity=0xf337023(243M) lifetime=0
  [g] #42 test_db/000041.log: size=0xf337023(243M) hint=2 len(extents)=15497
  [_] removed files
  [.] unwritten area

  0x000f00000000 ________________________________
  0x000f08000000 _ggggggggggggggggggggggggggggggg
  0x000f10000000 gggggggggggggggggggggggggggggggg
  0x000f18000000 gggggggggggg____________________
  0x000f20000000 ________________________________
  0x000f28000000 ________________________________
  0x000f30000000 ________________________________
  0x000f38000000 ________________________________
  0x000f40000000 ______________

Zone #28: start=0x000f80000000 wp=0x000fc3500000 remaining_capacity=0x0(0B) max_capacity=0x43500000(1G) used_capacity=0x101c071c(257M) lifetime=0
  [j] #297 test_db/000296.sst: size=0x80e024f(128M) hint=5 len(extents)=1
  [0] #314 test_db/000313.sst: size=0x80e04cd(128M) hint=3 len(extents)=1
  [_] removed files
  [.] unwritten area

  0x000f80000000 jjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjj
  0x000f88000000 j_______________________________
  0x000f90000000 ________________________________
  0x000f98000000 ________________________________
  0x000fa0000000 ___________________0000000000000
  0x000fa8000000 0000000000000000000_____________
  0x000fb0000000 ________________________________
  0x000fb8000000 ________________________________
  0x000fc0000000 ______________
```
