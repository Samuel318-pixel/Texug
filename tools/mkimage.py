#!/usr/bin/env python3
import pathlib
import struct
import sys

SECTOR_SIZE = 512
BOOT_SECTORS = 48
IMAGE_SECTORS = 131072
PARTITION_START = 2048
PARTITION_TYPE_FAT32_LBA = 0x0C
FAT_RESERVED_SECTORS = 32
FAT_COUNT = 2
FAT_ROOT_CLUSTER = 2


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit("usage: mkimage.py BOOT_SECTOR PAYLOAD OUTPUT")

    boot_path, payload_path, output_path = map(pathlib.Path, sys.argv[1:])
    boot = boot_path.read_bytes()
    payload = payload_path.read_bytes()

    if len(boot) != SECTOR_SIZE or boot[510:512] != b"\x55\xaa":
        raise SystemExit("boot sector must be exactly 512 bytes and end in 0x55aa")
    if not payload or len(payload) > BOOT_SECTORS * SECTOR_SIZE:
        raise SystemExit(
            f"payload must fit in {BOOT_SECTORS * SECTOR_SIZE} bytes"
        )

    partition_sectors = IMAGE_SECTORS - PARTITION_START
    fat_sectors = 1
    while True:
        cluster_count = (
            partition_sectors - FAT_RESERVED_SECTORS - FAT_COUNT * fat_sectors
        )
        required_fat_sectors = (
            (cluster_count + 2) * 4 + SECTOR_SIZE - 1
        ) // SECTOR_SIZE
        if required_fat_sectors == fat_sectors:
            break
        fat_sectors = required_fat_sectors

    image = bytearray(IMAGE_SECTORS * SECTOR_SIZE)
    image[:SECTOR_SIZE] = boot
    start = SECTOR_SIZE
    image[start : start + len(payload)] = payload

    partition_entry = (
        b"\x00\xfe\xff\xff"
        + bytes((PARTITION_TYPE_FAT32_LBA,))
        + b"\xfe\xff\xff"
        + struct.pack("<II", PARTITION_START, partition_sectors)
    )
    image[446:462] = partition_entry

    boot_sector = bytearray(SECTOR_SIZE)
    boot_sector[0:3] = b"\xeb\x58\x90"
    boot_sector[3:11] = b"TEXUGOS "
    struct.pack_into("<H", boot_sector, 11, SECTOR_SIZE)
    boot_sector[13] = 1
    struct.pack_into("<H", boot_sector, 14, FAT_RESERVED_SECTORS)
    boot_sector[16] = FAT_COUNT
    boot_sector[21] = 0xF8
    struct.pack_into("<H", boot_sector, 24, 63)
    struct.pack_into("<H", boot_sector, 26, 255)
    struct.pack_into("<I", boot_sector, 28, PARTITION_START)
    struct.pack_into("<I", boot_sector, 32, partition_sectors)
    struct.pack_into("<I", boot_sector, 36, fat_sectors)
    struct.pack_into("<I", boot_sector, 44, FAT_ROOT_CLUSTER)
    struct.pack_into("<H", boot_sector, 48, 1)
    struct.pack_into("<H", boot_sector, 50, 6)
    boot_sector[64] = 0x80
    boot_sector[66] = 0x29
    struct.pack_into("<I", boot_sector, 67, 0x54455847)
    boot_sector[71:82] = b"TEXUGOS    "
    boot_sector[82:90] = b"FAT32   "
    boot_sector[510:512] = b"\x55\xaa"
    volume_start = PARTITION_START * SECTOR_SIZE
    image[volume_start : volume_start + SECTOR_SIZE] = boot_sector
    image[volume_start + 6 * SECTOR_SIZE : volume_start + 7 * SECTOR_SIZE] = (
        boot_sector
    )

    fs_info = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", fs_info, 0, 0x41615252)
    struct.pack_into("<I", fs_info, 484, 0x61417272)
    struct.pack_into("<I", fs_info, 488, cluster_count - 1)
    struct.pack_into("<I", fs_info, 492, 3)
    struct.pack_into("<I", fs_info, 508, 0xAA550000)
    image[volume_start + SECTOR_SIZE : volume_start + 2 * SECTOR_SIZE] = fs_info
    image[volume_start + 7 * SECTOR_SIZE : volume_start + 8 * SECTOR_SIZE] = fs_info

    fat = bytearray(SECTOR_SIZE)
    struct.pack_into("<III", fat, 0, 0x0FFFFFF8, 0xFFFFFFFF, 0x0FFFFFFF)
    fat_start = PARTITION_START + FAT_RESERVED_SECTORS
    fat_offset = fat_start * SECTOR_SIZE
    fat_bytes = fat_sectors * SECTOR_SIZE
    image[fat_offset : fat_offset + SECTOR_SIZE] = fat
    second_fat_offset = (fat_start + fat_sectors) * SECTOR_SIZE
    image[second_fat_offset : second_fat_offset + SECTOR_SIZE] = fat

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(image)


if __name__ == "__main__":
    main()
