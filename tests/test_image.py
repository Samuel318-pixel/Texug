import pathlib
import struct
import unittest

IMAGE_PATH = pathlib.Path("build/texugos.img")
SECTOR_SIZE = 512
BOOT_PAYLOAD_SECTORS = 96


class BootImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = IMAGE_PATH.read_bytes()

    def test_raw_disk_image_has_expected_size_and_signature(self):
        self.assertEqual(len(self.image), 131072 * SECTOR_SIZE)
        self.assertEqual(self.image[510:512], b"\x55\xaa")

    def test_image_contains_fat32_lba_partition(self):
        partition_type = self.image[450]
        start_sector, sector_count = struct.unpack_from("<II", self.image, 454)
        self.assertEqual(partition_type, 0x0C)
        self.assertEqual(start_sector, 2048)
        self.assertEqual(sector_count, 131072 - start_sector)

        volume = start_sector * SECTOR_SIZE
        self.assertEqual(self.image[volume + 82 : volume + 90], b"FAT32   ")
        self.assertEqual(self.image[volume + 510 : volume + 512], b"\x55\xaa")

        reserved_sectors = struct.unpack_from("<H", self.image, volume + 14)[0]
        fat_count = self.image[volume + 16]
        fat_size = struct.unpack_from("<I", self.image, volume + 36)[0]
        root_cluster = struct.unpack_from("<I", self.image, volume + 44)[0]
        first_fat = volume + reserved_sectors * SECTOR_SIZE
        self.assertEqual(struct.unpack_from("<I", self.image, first_fat + 8)[0], 0x0FFFFFFF)
        self.assertEqual(fat_count, 2)
        self.assertEqual(root_cluster, 2)
        self.assertGreater(fat_size, 0)

    def test_boot_sector_loads_fixed_payload_from_lba_one(self):
        packet = self.image[:SECTOR_SIZE]
        self.assertIn(b"TexugOS: BIOS boot", packet)
        self.assertIn(b"TexugOS: payload read failed", packet)
        self.assertIn(struct.pack("<H", BOOT_PAYLOAD_SECTORS), packet)
        self.assertIn(struct.pack("<Q", 1), packet)

    def test_payload_contains_boot_handoff_and_e820_messages(self):
        payload = self.image[
            SECTOR_SIZE : (BOOT_PAYLOAD_SECTORS + 1) * SECTOR_SIZE
        ]
        self.assertIn(b"TexugOS: collecting E820 memory map", payload)
        self.assertIn(b"TexugOS: E820 memory map failed", payload)
        self.assertIn(b"Kernel subsystems active (partial).", payload)
        self.assertIn(struct.pack("<I", 0x54455847), payload)


if __name__ == "__main__":
    unittest.main()
