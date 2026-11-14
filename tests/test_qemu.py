import json
import pathlib
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import unittest

IMAGE_PATH = pathlib.Path("build/texugos.img").resolve()
VGA_BYTES = 80 * 25 * 2


@unittest.skipUnless(shutil.which("qemu-system-i386"), "QEMU is not installed")
class QemuBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory(prefix="texugos-qemu-")
        cls.qemu_image = pathlib.Path(cls.tempdir.name) / "texugos.img"
        shutil.copyfile(IMAGE_PATH, cls.qemu_image)
        cls.qmp_path = pathlib.Path(cls.tempdir.name) / "qmp.sock"
        cls.vga_path = pathlib.Path(cls.tempdir.name) / "vga.bin"
        cls.process = subprocess.Popen(
            [
                "qemu-system-i386",
                "-display",
                "none",
                "-no-reboot",
                "-no-shutdown",
                "-drive",
                f"file={cls.qemu_image},format=raw,if=ide",
                "-boot",
                "c",
                "-qmp",
                f"unix:{cls.qmp_path},server=on,wait=off",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        try:
            cls._connect_qmp()
            cls._execute_qmp({"execute": "qmp_capabilities"})
            cls.screen = ""
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                cls.screen = cls._read_screen()
                if "TexugOS shell ready." in cls.screen:
                    break
                time.sleep(0.1)
            else:
                visible = "\n".join(
                    line.rstrip()
                    for line in (
                        cls.screen[index : index + 80]
                        for index in range(0, len(cls.screen), 80)
                    )
                    if line.strip()
                )
                raise TimeoutError(
                    f"TexugOS did not finish booting in QEMU; VGA:\n{visible}"
                )
        except Exception:
            cls._stop_qemu()
            cls.tempdir.cleanup()
            raise

    @classmethod
    def _connect_qmp(cls):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if cls.process.poll() is not None:
                raise RuntimeError("QEMU exited before the QMP socket became ready")
            try:
                cls.qmp_socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                cls.qmp_socket.settimeout(2)
                cls.qmp_socket.connect(str(cls.qmp_path))
                cls.qmp_file = cls.qmp_socket.makefile("rwb", buffering=0)
                json.loads(cls.qmp_file.readline())
                return
            except (FileNotFoundError, ConnectionRefusedError):
                cls.qmp_socket.close()
                time.sleep(0.05)
        raise TimeoutError("QEMU QMP socket did not become ready")

    @classmethod
    def _execute_qmp(cls, command):
        cls.qmp_file.write((json.dumps(command) + "\n").encode())
        while True:
            response = json.loads(cls.qmp_file.readline())
            if "return" in response:
                return response["return"]
            if "error" in response:
                raise RuntimeError(response["error"])

    @classmethod
    def _read_screen(cls):
        cls._execute_qmp(
            {
                "execute": "human-monitor-command",
                "arguments": {
                    "command-line": (
                        f'pmemsave 0xb8000 0x{VGA_BYTES:x} "{cls.vga_path}"'
                    )
                },
            }
        )
        if not cls.vga_path.exists() or cls.vga_path.stat().st_size != VGA_BYTES:
            return ""
        memory = cls.vga_path.read_bytes()
        return "".join(chr(memory[index]) for index in range(0, len(memory), 2))

    @classmethod
    def _send_command(cls, command):
        for character in command + "\n":
            qcode = {" ": "spc", "\n": "ret", ".": "dot"}.get(
                character, character
            )
            cls._execute_qmp(
                {
                    "execute": "send-key",
                    "arguments": {
                        "keys": [{"type": "qcode", "data": qcode}],
                        "hold-time": 10,
                    },
                }
            )
            time.sleep(0.01)

    @classmethod
    def _send_mouse_move(cls, x, y):
        cls._execute_qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [
                        {"type": "rel", "data": {"axis": "x", "value": x}},
                        {"type": "rel", "data": {"axis": "y", "value": y}},
                    ]
                },
            }
        )

    def _run_shell_command(self, command, expected_output):
        self._send_command(command)
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            command_position = screen.rfind(command)
            while command_position >= 0:
                prompt_before = screen.rfind("texugos:", 0, command_position)
                if prompt_before // 80 == command_position // 80:
                    break
                command_position = screen.rfind(command, 0, command_position)
            if command_position >= 0:
                last_command_character = command_position + len(command) - 1
                output_start = (last_command_character // 80 + 1) * 80
                next_prompt = screen.find("texugos:", output_start)
            else:
                output_start = len(screen)
                next_prompt = -1
            if (
                expected_output in screen[output_start:]
                and next_prompt >= 0
            ):
                return screen
            time.sleep(0.05)
        self.fail(
            f"Shell command {command!r} did not produce {expected_output!r}; "
            f"VGA:\n{screen}"
        )

    @classmethod
    def _stop_qemu(cls):
        if hasattr(cls, "qmp_file"):
            cls.qmp_file.close()
        if hasattr(cls, "qmp_socket"):
            cls.qmp_socket.close()
        if cls.process.poll() is None:
            cls.process.send_signal(signal.SIGTERM)
            try:
                cls.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                cls.process.kill()
                cls.process.wait(timeout=5)

    @classmethod
    def tearDownClass(cls):
        cls._stop_qemu()
        cls.tempdir.cleanup()

    def test_kernel_reports_working_memory_and_interrupt_setup(self):
        self.assertIn("Usable RAM:", self.screen)
        self.assertIn("Paging: 16 MiB identity map active", self.screen)
        self.assertIn("Memory: frame allocator and kernel heap active", self.screen)
        self.assertIn("ATA: QEMU HARDDISK", self.screen)
        self.assertIn("FAT32: mount and CRUD test passed", self.screen)
        self.assertIn("Keyboard: PS/2 input enabled", self.screen)
        self.assertIn("Mouse: PS/2 initialized", self.screen)
        self.assertIn("IDT: exceptions and IRQ vectors installed", self.screen)
        self.assertIn("PIC: remapped; PIT: 100 Hz", self.screen)
        self.assertIn("Threads: 2 cooperative tasks completed", self.screen)
        self.assertIn("Kernel stage 6 active (partial).", self.screen)
        self.assertIn("TexugOS shell ready.", self.screen)
        self.assertRegex(self.screen, r"PIT ticks: [1-9][0-9]*")

    def test_shell_commands_and_fat32_navigation(self):
        self._run_shell_command("help", "Commands: help clear echo ls cd mkdir touch cat rm")
        self._send_mouse_move(25, 15)
        mouse_screen = self._run_shell_command("mouse", "Mouse packets:")
        mouse_packets = int(
            mouse_screen.split("Mouse packets: ", 1)[1].split()[0]
        )
        self.assertGreater(mouse_packets, 0)
        position = mouse_screen.split(" position ", 1)[1].split(" buttons", 1)[0]
        self.assertNotEqual(position, "320,100")
        self._run_shell_command("version", "TexugOS version 0.1.0")
        self._run_shell_command("echo ready", "ready")
        self._run_shell_command("mkdir test", "Directory created.")
        screen = self._run_shell_command("cd test", "texugos:/TEST >")
        self.assertIn("texugos:/TEST >", screen)
        self._run_shell_command("mkdir child", "Directory created.")
        self._run_shell_command("cd child", "texugos:/TEST/CHILD >")
        self._run_shell_command("cd ..", "texugos:/TEST >")
        self._run_shell_command("rm child", "Removed.")
        self._run_shell_command("touch note", "File created.")
        self._run_shell_command("ls", "NOTE  0 bytes")
        screen = self._run_shell_command("cat note", "texugos:/TEST >")
        self.assertNotIn("Unable to read file", screen)
        self._run_shell_command("rm note", "Removed.")
        self._run_shell_command("cd", "texugos:/ >")
        self._run_shell_command("rm test", "Removed.")
        self._run_shell_command("memory", "Free physical frames:")
        self._run_shell_command("processes", "User processes: 0")

        self._send_command("clear")
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if screen.startswith("texugos:/ >"):
                break
            time.sleep(0.05)
        self.assertTrue(screen.startswith("texugos:/ >"), screen)


if __name__ == "__main__":
    unittest.main()
