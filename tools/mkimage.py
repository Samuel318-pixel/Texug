#!/usr/bin/env python3
import pathlib
import sys

SECTOR_SIZE = 512
BOOT_SECTORS = 32
IMAGE_SECTORS = 32768


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

    image = bytearray(IMAGE_SECTORS * SECTOR_SIZE)
    image[:SECTOR_SIZE] = boot
    start = SECTOR_SIZE
    image[start : start + len(payload)] = payload
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(image)


if __name__ == "__main__":
    main()
