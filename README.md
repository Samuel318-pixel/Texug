# TexugOS

TexugOS is an experimental x86 operating system being developed in stages. Its
current milestones cover the Legacy BIOS boot path, storage, shell, emulated
input, and a basic graphical desktop.

## Current implementation (partial)

The boot image contains a 512-byte boot sector followed by a fixed 104-sector
payload and a FAT32 partition. The boot sector uses BIOS LBA extensions when
available and falls back to BIOS CHS reads when needed, loads the payload at
physical address `0x10000`, and transfers control to it. The payload collects
the BIOS E820 memory map, enables A20, enters 32-bit protected mode, and calls a
freestanding C entry point. The entry point reports usable RAM, initializes
and enables a 16 MiB identity-mapped paging region, installs exception and IRQ
handlers in the IDT, remaps the 8259 PIC, and starts the PIT at 100 Hz. Timer
interrupts are acknowledged and counted. A physical-frame bitmap is populated
from E820, the kernel has a small coalescing heap, and a preemptive scheduler
can switch between kernel threads with separate stacks. A polled primary-master
ATA PIO driver mounts the image's FAT32 partition and supports 8.3 aliases,
ASCII VFAT long names, and file/directory creation, reading, and deletion. The
kernel also provides PCI enumeration for hardware identification, PS/2
keyboard and mouse IRQ
handlers, and an interactive text shell with
`help`, `clear`, `echo`, `ls`, `cd`, `mkdir`, `touch`, `cat`, `rm`, `reboot`,
`shutdown`, `memory`, `version`, `processes`, and `mouse` commands.

The default image is a raw, partitioned 64 MiB BIOS disk image. `make iso`
packages it in a BIOS El Torito ISO using hard-disk emulation. The ISO still
requires a separate writable ATA disk for FAT32 storage; `make run-iso`
attaches the raw image as that disk. It is not an installer.

### Build and test

Required tools: GNU Make, GCC with 32-bit compilation support, GNU binutils,
Python 3, and QEMU's `qemu-system-i386` for runtime testing. The ISO targets
also require `xorriso`.

```sh
make
make test
make run
make iso
make test-iso
make run-iso
```

`make test` builds the image, checks its boot-sector signature and FAT32
partition layout, and runs a headless QEMU test. It checks the kernel's boot
status and sends keyboard and mouse input to exercise shell commands, PCI
device listing, FAT32 short and long filename CRUD, directory navigation, and
PS/2 mouse packet handling.
It also verifies the 320×200 VGA desktop, opens and closes a window using the
mouse, exercises the graphical terminal and FAT32 file browser, checks the
system-information app, starts the native raycasting arena, verifies enemy
pursuit and tests its shooting and mouse-aim controls, obtains a DHCP lease,
pings the QEMU network gateway,
resolves `localhost` through the DHCP-provided DNS server,
loads a page from a local HTTP test server through QEMU networking using a
formatted HTTP URL, and
confirms that Escape restores text mode.
`make test-iso` separately boots the ISO through SeaBIOS and checks the kernel
and FAT32 test with a writable ATA disk attached.
The kernel also creates a directory and a 700-byte file, verifies its contents,
then removes both.
`make run` starts QEMU with the raw disk image.
The HP G42-215BR hardware has not been inspected; no device-specific drivers
are claimed or included.

## Current status and limitations

- Implemented: BIOS boot sector, LBA disk read, E820 handoff, A20 enable,
  protected-mode transition, initial 16 MiB paging, CPU exceptions, PIC IRQ
  handling, 100 Hz PIT timer, E820-backed frame allocation, kernel heap, and
  preemptive kernel threads, ATA PIO, FAT32 file/directory operations, a
  PCI enumeration, PS/2 keyboard-driven shell and mouse input, and a
  320×200 VGA desktop with clickable icons, a taskbar, pointer, and
  movable/resizable/minimizable/maximizable windows. The desktop includes
  kernel-integrated Files, Terminal, and System Information apps; redraws are
  limited to 50 Hz while input continues to be processed each loop. The
  terminal supports basic FAT32 and system commands. Networking has a polled
  RTL8139 driver, DHCP configuration, ARP, IPv4 ICMP echo, and `net`, `dhcp`,
  `dns` A-record lookup and `ping` shell commands. A text browser can fetch
  HTTP/1.0 pages using IPv4 addresses or DNS names over a minimal TCP client.
  A BIOS El Torito ISO target is available. HTTPS is not supported. The
  desktop also has a native, two-level raycasting game with movement, mouse
  aiming, shooting, health, ammo, enemies, and wall collision.
- Partial: DNS currently resolves IPv4 A records only. The game has two
  compact arena layouts; QEMU tests combat in the first and progression to
  the second. Enemies pursue the player, and the PC speaker provides firing,
  hit, damage, and reload tones; audible output is unverified. Stage 6 device
  support and all networking have only been exercised with QEMU-emulated
  hardware.
  Desktop apps and the game run inside the kernel; user-mode application
  processes and isolation are not implemented. Networking has no TLS/HTTPS,
  JavaScript, or general-purpose TCP sockets and has only been tested with
  QEMU user-mode networking.
- Partial: FAT32 long filenames support ASCII names up to 255 characters with
  generated 8.3 aliases; Unicode names are not supported.
- Not yet implemented: AHCI/SATA hardware verification, a full-featured
  browser, or user-mode applications.
- The QEMU smoke test confirms boot in emulation; physical hardware has not
  been tested.
- The ISO boots through BIOS El Torito hard-disk emulation; the kernel's ATA
  driver still needs a separate writable disk. No installer is included.
- The ATA device is QEMU's emulated disk; physical HP G42-215BR hardware is
  unavailable for identification and testing.
- The PS/2 keyboard and mouse are QEMU-emulated devices; the notebook's
  touchpad and actual controller models have not been identified.
- ATA writes are polled to command completion, but the driver does not issue
  an ATA cache-flush command; power-loss durability is not guaranteed.
- `shutdown` halts the CPU; ACPI power-off support is not implemented.
