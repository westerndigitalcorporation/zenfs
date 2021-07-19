#!/usr/bin/env python3

import cysimdjson
import string
from dataclasses import dataclass
import sys
from hurry.filesize import size
from tqdm import tqdm


@dataclass
class Extent:
    file_id: int
    start: int
    length: int


@dataclass
class File:
    file_id: int
    filename: str
    size: int
    hint: int
    extents: list


@dataclass
class Zone:
    zone_id: int
    start: int
    capacity: int
    max_capacity: int
    wp: int
    lifetime: int
    used_capacity: int


DISPLAY_CHARACTERS = list(string.ascii_lowercase + string.digits)


def stat_zone_file(zone):
    file_ids = {}
    for pos, extent in zone.items():
        file_ids[extent.file_id] = True
    return file_ids


def insert_newlines(string, every=64):
    return '\n'.join(string[i:i+every] for i in range(0, len(string), every))


def file_symbol(id):
    return DISPLAY_CHARACTERS[id % len(DISPLAY_CHARACTERS)]


def load_data(json_file):
    print("loading data...")
    with open(json_file, 'rb') as f:
        parser = cysimdjson.JSONParser()
        data = parser.parse(f.read())
    print("processing data...")
    return data


def size_human(value):
    return f"0x{value:x}({size(value)})"


def print_zone(zone):
    print(f"Zone #{zone.zone_id}: start=0x{(zone.start):012x} wp=0x{(zone.wp):012x} remaining_capacity={size_human(zone.capacity)} " +
          f"max_capacity={size_human(zone.max_capacity)} used_capacity={size_human(zone.used_capacity)} lifetime={zone.lifetime}")


def to_zone(input_data):
    idx, zone_json = input_data
    return Zone(idx, zone_json["start"], zone_json["capacity"], zone_json["max_capacity"], zone_json["wp"], zone_json["lifetime"], zone_json["used_capacity"])


def get_meta_zones(data):
    return list(map(to_zone, enumerate(data["zones"]["meta"])))


def get_io_zones(data):
    return list(map(to_zone, enumerate(data["zones"]["io"])))


def cmd_zones(data):
    total_capacity = 0
    used_capacity = 0
    remaining_capacity = 0
    reclaimable_capacity = 0

    print("--- META ZONES ---")
    for zone in get_meta_zones(data):
        total_capacity += zone.max_capacity
        remaining_capacity += zone.capacity
        used_capacity += zone.used_capacity
        print_zone(zone)

    print("--- IO ZONES ---")
    for zone in get_io_zones(data):
        total_capacity += zone.max_capacity
        remaining_capacity += zone.capacity
        used_capacity += zone.used_capacity
        if zone.capacity == 0:
            reclaimable_capacity += zone.max_capacity - zone.used_capacity
        print_zone(zone)

    print(f"total={size_human(total_capacity)} free={size_human(remaining_capacity)} used={size_human(used_capacity)} reclaimable={size_human(reclaimable_capacity)}")


def to_file(file_json):
    file_id = file_json['id']
    return File(file_id, file_json['filename'], file_json['size'], file_json['hint'], file_json['extents'])


def get_files(data):
    return list(map(to_file, data["files"]))


def print_file(file):
    print(f"#{file.file_id} {file.filename}: size={size_human(file.size)} hint={file.hint} len(extents)={len(file.extents)}")


def cmd_files(data):
    print("--- FILES ---")
    files = sorted(get_files(data), key=lambda x: x.file_id)
    zones = sorted(get_io_zones(data), key=lambda x: x.start)
    total_extents = 0
    for file in files:
        print_file(file)
        last_zone = zones[0]
        zone_id = 0
        extent_counter = 0
        extent_size = 0
        total_extents += len(file.extents)
        for extent in sorted(file.extents, key=lambda x: x["start"]):
            while extent["start"] > last_zone.start + last_zone.max_capacity:
                if extent_counter != 0:
                    print(
                        f"  Zone #{zone_id}: len(extents)={extent_counter} size={size_human(extent_size)}")
                    extent_counter = 0
                    extent_size = 0
                zone_id += 1
                last_zone = zones[zone_id]
            extent_counter += 1
            extent_size += extent["length"]
        if extent_counter != 0:
            print(
                f"  Zone #{zone_id}: len(extents)={extent_counter} size={size_human(extent_size)}")
            extent_counter = 0
    print(f"{total_extents} extents in total")


def cmd_extents(data):
    files = sorted(get_files(data), key=lambda x: x.file_id)
    files_map = dict(zip(map(lambda x: x.file_id, files), files))
    zones = sorted(get_io_zones(data), key=lambda x: x.start)
    extents_in_zone = {}
    for file in tqdm(files):
        last_zone = zones[0]
        zone_id = 0
        for extent in sorted(file.extents, key=lambda x: x["start"]):
            while extent["start"] > last_zone.start + last_zone.max_capacity:
                zone_id += 1
                last_zone = zones[zone_id]
            extent_list = extents_in_zone.get(zone_id, [])
            extent_list.append(
                Extent(file.file_id, extent["start"], extent["length"]))
            extents_in_zone[zone_id] = extent_list

    chunk_size = 4 * 1024 * 1024  # chunk size = 4MB
    for zone in zones:
        print_zone(zone)
        if zone.zone_id in extents_in_zone:
            extents = extents_in_zone[zone.zone_id]
            extents = sorted(extents, key=lambda x: x.start)
        else:
            extents = []

        file_ids = sorted(set(map(lambda x: x.file_id, extents)))
        for file_id in file_ids:
            print(f"  [{file_symbol(file_id)}] ", end="")
            print_file(files_map[file_id])
        print("  [_] removed files")
        print("  [.] unwritten area")

        chunk_total = (zone.max_capacity + chunk_size - 1) // chunk_size
        chunk_start = zone.start
        current_extent = 0
        for chunk_id in range(chunk_total):
            if chunk_id % 32 == 0:
                print()
                print(f"  0x{chunk_start:012x} ", end="")
            if chunk_start >= zone.wp:
                print(".", end="")
            else:
                while current_extent < len(extents) and extents[current_extent].start + extents[current_extent].length <= chunk_start:
                    current_extent += 1
                if current_extent < len(extents) and extents[current_extent].start <= chunk_start <= extents[current_extent].start + extents[current_extent].length:
                    print(file_symbol(extents[current_extent].file_id), end="")
                else:
                    print("_", end="")
            chunk_start += chunk_size
        print()
        print()


def main(argv):
    cmd = argv[1]
    json_file = argv[2] if len(argv) > 2 else "result.json"
    if cmd == 'zones':
        cmd_zones(load_data(json_file))
    elif cmd == 'files':
        cmd_files(load_data(json_file))
    elif cmd == 'extents':
        cmd_extents(load_data(json_file))
    else:
        print("Please specify a subcommand. See README.md for usage.")


if __name__ == '__main__':
    main(sys.argv)
