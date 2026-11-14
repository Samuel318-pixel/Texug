import pathlib
import struct
import unittest

IMAGE_PATH = pathlib.Path("build/texugos.img")
SECTOR_SIZE = 512


class BootImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.image = IMAGE_PATH.read_bytes()

    def test_raw_disk_image_has_expected_size_and_signature(self):
        self.assertEqual(len(self.image), 32768 * SECTOR_SIZE)
        self.assertEqual(self.image[510:512], b"\x55\xaa")

    def test_boot_sector_loads_fixed_payload_from_lba_one(self):
        packet = self.image[:SECTOR_SIZE]
        self.assertIn(b"TexugOS: BIOS boot", packet)
        self.assertIn(b"TexugOS: payload read failed", packet)
        self.assertIn(struct.pack("<H", 32), packet)
        self.assertIn(struct.pack("<Q", 1), packet)

    def test_payload_contains_boot_handoff_and_e820_messages(self):
        payload = self.image[SECTOR_SIZE : 33 * SECTOR_SIZE]
        self.assertIn(b"TexugOS: collecting E820 memory map", payload)
        self.assertIn(b"TexugOS: E820 memory map failed", payload)
        self.assertIn(b"Kernel stage 3 active.", payload)
        self.assertIn(struct.pack("<I", 0x54455847), payload)


if __name__ == "__main__":
    unittest.main()
