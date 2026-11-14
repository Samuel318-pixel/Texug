import json
import http.server
import pathlib
import shutil
import signal
import socket
import subprocess
import tempfile
import threading
import time
import unittest

IMAGE_PATH = pathlib.Path("build/texugos.img").resolve()
VGA_BYTES = 80 * 25 * 2
GRAPHICS_BYTES = 320 * 200


class BrowserTestHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"TEXUGOS BROWSER TEST\r\n"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format_string, *args):
        pass


@unittest.skipUnless(shutil.which("qemu-system-i386"), "QEMU is not installed")
class QemuBootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory(prefix="texugos-qemu-")
        cls.http_server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0), BrowserTestHandler
        )
        cls.http_port = cls.http_server.server_address[1]
        cls.http_thread = threading.Thread(
            target=cls.http_server.serve_forever, daemon=True
        )
        cls.http_thread.start()
        cls.qemu_image = pathlib.Path(cls.tempdir.name) / "texugos.img"
        cls.framebuffer_path = pathlib.Path(cls.tempdir.name) / "framebuffer.bin"
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
                "-netdev",
                "user,id=net0",
                "-device",
                "rtl8139,netdev=net0",
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
            cls.http_server.shutdown()
            cls.http_server.server_close()
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
            qcodes = {
                " ": ["spc"],
                "\n": ["ret"],
                ".": ["dot"],
                ":": ["shift", "semicolon"],
                "/": ["slash"],
            }.get(character, [character])
            cls._execute_qmp(
                {
                    "execute": "send-key",
                    "arguments": {
                        "keys": [
                            {"type": "qcode", "data": qcode}
                            for qcode in qcodes
                        ],
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

    @classmethod
    def _send_mouse_button(cls, down):
        cls._execute_qmp(
            {
                "execute": "input-send-event",
                "arguments": {
                    "events": [
                        {
                            "type": "btn",
                            "data": {"button": "left", "down": down},
                        }
                    ]
                },
            }
        )

    @classmethod
    def _move_mouse_to(cls, current_x, current_y, target_x, target_y):
        delta_x = target_x - current_x
        delta_y = target_y - current_y
        while delta_x != 0 or delta_y != 0:
            step_x = max(-127, min(127, delta_x))
            step_y = max(-127, min(127, delta_y))
            cls._send_mouse_move(step_x, step_y)
            delta_x -= step_x
            delta_y -= step_y
        time.sleep(0.1)

    @classmethod
    def _click_mouse(cls):
        cls._send_mouse_button(True)
        time.sleep(0.1)
        cls._send_mouse_button(False)
        time.sleep(0.1)

    @classmethod
    def _send_key(cls, key):
        cls._execute_qmp(
            {
                "execute": "send-key",
                "arguments": {
                    "keys": [{"type": "qcode", "data": key}],
                    "hold-time": 10,
                },
            }
        )

    @classmethod
    def _read_framebuffer(cls):
        cls._execute_qmp(
            {
                "execute": "human-monitor-command",
                "arguments": {
                    "command-line": (
                        f'pmemsave 0xa0000 0x{GRAPHICS_BYTES:x} '
                        f'"{cls.framebuffer_path}"'
                    )
                },
            }
        )
        if (
            not cls.framebuffer_path.exists()
            or cls.framebuffer_path.stat().st_size != GRAPHICS_BYTES
        ):
            return b""
        return cls.framebuffer_path.read_bytes()

    @staticmethod
    def _framebuffer_region(framebuffer, x, y, width, height):
        return b"".join(
            framebuffer[row * 320 + x : row * 320 + x + width]
            for row in range(y, y + height)
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
        cls.http_server.shutdown()
        cls.http_server.server_close()
        cls.http_thread.join(timeout=1)
        cls.tempdir.cleanup()

    def test_kernel_reports_working_memory_and_interrupt_setup(self):
        self.assertIn("Usable RAM:", self.screen)
        self.assertIn("Paging: 16 MiB identity map active", self.screen)
        self.assertIn("Memory: frame allocator and kernel heap active", self.screen)
        self.assertRegex(self.screen, r"PCI: [1-9][0-9]* devices enumerated")
        self.assertIn("Network: RTL8139 ready", self.screen)
        self.assertIn("ATA: QEMU HARDDISK", self.screen)
        self.assertIn("FAT32: mount and CRUD test passed", self.screen)
        self.assertIn("Keyboard: PS/2 input enabled", self.screen)
        self.assertIn("Mouse: PS/2 initialized", self.screen)
        self.assertIn("IDT: exceptions and IRQ vectors installed", self.screen)
        self.assertIn("PIC: remapped; PIT: 100 Hz", self.screen)
        self.assertIn("Threads: 2 cooperative tasks completed", self.screen)
        self.assertIn("Kernel stages 6-10 active (partial).", self.screen)
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
        devices = self._run_shell_command("devices", "PCI devices:")
        self.assertIn("PCI 0:", devices)
        self._run_shell_command("net", "IPv4: not configured.")
        self._run_shell_command("ping invalid", "Usage: ping A.B.C.D")
        self._run_shell_command("dhcp", "DHCP lease acquired.")
        network = self._run_shell_command("net", "IPv4: 10.0.2.15")
        self.assertIn("Gateway: 10.0.2.2", network)
        self._run_shell_command("ping 10.0.2.2", "Reply from 10.0.2.2")
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

    def test_z_graphical_desktop_and_text_mode_return(self):
        mouse_screen = self._run_shell_command("mouse", "Mouse packets:")
        mouse_position = mouse_screen.rsplit(" position ", 1)[1].split(
            " buttons", 1
        )[0]
        current_x, current_y = (
            int(value) for value in mouse_position.split(",")
        )
        self._send_command("desktop")
        deadline = time.monotonic() + 3
        framebuffer = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            if len(framebuffer) == GRAPHICS_BYTES and framebuffer[0] == 1:
                break
            time.sleep(0.05)
        self.assertEqual(len(framebuffer), GRAPHICS_BYTES)
        self.assertEqual(framebuffer[0], 1)

        sample = 120 * 320 + 250
        wallpaper_pixel = framebuffer[sample]

        self._move_mouse_to(current_x, current_y, 50, 75)
        self._click_mouse()
        deadline = time.monotonic() + 3
        framebuffer = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            if len(framebuffer) == GRAPHICS_BYTES and framebuffer[sample] == 15:
                break
            time.sleep(0.05)
        self.assertNotEqual(wallpaper_pixel, 15)
        window_pixels = [
            framebuffer[y * 320 + x]
            for y in (35, 40, 50, 75, 120, 160)
            for x in (80, 90, 150, 250, 270)
        ]
        self.assertEqual(
            framebuffer[sample],
            15,
            f"mouse started at {current_x},{current_y}; "
            f"window-region pixels: {window_pixels}",
        )

        terminal_before = self._framebuffer_region(framebuffer, 80, 52, 184, 100)
        self._send_command("echo ready")
        deadline = time.monotonic() + 3
        terminal_after = terminal_before
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            terminal_after = self._framebuffer_region(
                framebuffer, 80, 52, 184, 100
            )
            if terminal_after != terminal_before:
                break
            time.sleep(0.05)
        self.assertNotEqual(terminal_after, terminal_before)
        self._send_command("touch appfile")

        self._move_mouse_to(50, 75, 520, 40)
        self._click_mouse()
        deadline = time.monotonic() + 3
        framebuffer = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            if len(framebuffer) == GRAPHICS_BYTES and framebuffer[sample] == wallpaper_pixel:
                break
            time.sleep(0.05)
        self.assertEqual(framebuffer[sample], wallpaper_pixel)

        self._move_mouse_to(520, 40, 50, 50)
        self._click_mouse()
        deadline = time.monotonic() + 3
        framebuffer = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            if len(framebuffer) == GRAPHICS_BYTES and framebuffer[sample] == 15:
                break
            time.sleep(0.05)
        self.assertEqual(framebuffer[sample], 15)
        files_before = self._framebuffer_region(framebuffer, 75, 45, 190, 110)
        self._send_key("ret")
        deadline = time.monotonic() + 3
        files_after = files_before
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            files_after = self._framebuffer_region(
                framebuffer, 75, 45, 190, 110
            )
            if files_after != files_before:
                break
            time.sleep(0.05)
        self.assertNotEqual(files_after, files_before)

        self._move_mouse_to(50, 50, 520, 40)
        self._click_mouse()
        self._move_mouse_to(520, 40, 50, 125)
        self._click_mouse()
        deadline = time.monotonic() + 3
        framebuffer = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            settings_text_pixels = sum(
                framebuffer[row * 320 + column] == 1
                for row in range(64, 71)
                for column in range(96, 205)
            ) if len(framebuffer) == GRAPHICS_BYTES else 0
            if settings_text_pixels > 20:
                break
            time.sleep(0.05)
        self.assertGreater(settings_text_pixels, 20)
        self._move_mouse_to(50, 125, 550, 50)
        self._click_mouse()
        self._send_key("4")
        deadline = time.monotonic() + 3
        framebuffer = b""
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            if len(framebuffer) == GRAPHICS_BYTES and framebuffer[sample] == 15:
                break
            time.sleep(0.05)
        self.assertEqual(framebuffer[sample], 15)
        browser_before = self._framebuffer_region(
            framebuffer, 100, 70, 180, 90
        )
        self._send_command(f"10.0.2.2:{self.http_port}")
        deadline = time.monotonic() + 3
        browser_input = browser_before
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            browser_input = self._framebuffer_region(
                framebuffer, 100, 70, 180, 90
            )
            if browser_input != browser_before:
                break
            time.sleep(0.05)
        self.assertNotEqual(browser_input, browser_before)
        browser_page_before = browser_input
        self._send_key("ret")
        deadline = time.monotonic() + 10
        browser_after = browser_page_before
        browser_text_pixels = 0
        while time.monotonic() < deadline:
            framebuffer = self._read_framebuffer()
            browser_after = self._framebuffer_region(
                framebuffer, 100, 70, 180, 90
            )
            browser_text_pixels = sum(
                framebuffer[row * 320 + column] == 1
                for row in range(91, 99)
                for column in range(104, 224)
            ) if len(framebuffer) == GRAPHICS_BYTES else 0
            if browser_after != browser_page_before and browser_text_pixels > 30:
                break
            time.sleep(0.05)
        self.assertNotEqual(browser_after, browser_page_before)
        self.assertGreater(browser_text_pixels, 30)
        self._send_key("esc")
        deadline = time.monotonic() + 3
        screen = ""
        while time.monotonic() < deadline:
            screen = self._read_screen()
            if "Returned from TexugOS Desktop." in screen:
                break
            time.sleep(0.05)
        self.assertIn("Returned from TexugOS Desktop.", screen)


if __name__ == "__main__":
    unittest.main()
