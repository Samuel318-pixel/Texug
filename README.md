# TexugOS

TexugOS is an experimental x86 operating system being developed in stages. Its
current milestones cover the Legacy BIOS boot path, storage, shell, and basic
emulated input.

## Implemented: Stages 1-5; Stage 6 (partial)

The boot image contains a 512-byte boot sector followed by a fixed 48-sector
payload and a FAT32 partition. The boot sector checks BIOS LBA-extension
support, loads the payload at physical address `0x10000`, and transfers control
to it. The payload collects
the BIOS E820 memory map, enables A20, enters 32-bit protected mode, and calls a
freestanding C entry point. The entry point reports usable RAM, initializes
and enables a 16 MiB identity-mapped paging region, installs exception and IRQ
handlers in the IDT, remaps the 8259 PIC, and starts the PIT at 100 Hz. Timer
interrupts are acknowledged and counted. A physical-frame bitmap is populated
from E820, the kernel has a small coalescing heap, and a cooperative scheduler
can switch between kernel threads with separate stacks. A polled primary-master
ATA PIO driver mounts the image's FAT32 partition and supports short-name file
and directory creation, reading, and deletion. The kernel also provides a
PCI enumeration for hardware identification, PS/2 keyboard and mouse IRQ
handlers, and an interactive text shell with
`help`, `clear`, `echo`, `ls`, `cd`, `mkdir`, `touch`, `cat`, `rm`, `reboot`,
`shutdown`, `memory`, `version`, `processes`, and `mouse` commands.

The image is a raw, partitioned 64 MiB BIOS disk image. It is not an ISO and
is not yet an installer.

### Build and test

Required tools: GNU Make, GCC with 32-bit compilation support, GNU binutils,
Python 3, and QEMU's `qemu-system-i386` for runtime testing.

```sh
make
make test
make run
```

`make test` builds the image, checks its boot-sector signature and FAT32
partition layout, and runs a headless QEMU test. It checks the kernel's boot
status and sends keyboard and mouse input to exercise shell commands, PCI
device listing, FAT32 directory navigation, and PS/2 mouse packet handling.
The kernel also creates a directory and a 700-byte file, verifies its contents,
then removes both.
`make run` starts QEMU with the raw disk image.
The HP G42-215BR hardware has not been inspected; no device-specific drivers
are claimed or included.

## Current status and limitations

- Implemented: BIOS boot sector, LBA disk read, E820 handoff, A20 enable,
  protected-mode transition, initial 16 MiB paging, CPU exceptions, PIC IRQ
  handling, 100 Hz PIT timer, E820-backed frame allocation, kernel heap, and
  cooperative kernel threads, ATA PIO, FAT32 file/directory operations, a
  PCI enumeration, PS/2 keyboard-driven shell, and basic PS/2 mouse packet
  input.
- Not yet implemented: user-mode process isolation, preemptive scheduling,
  AHCI/SATA hardware verification, long filenames, graphical mouse cursor,
  graphics desktop, networking, or apps.
- The QEMU smoke test confirms boot in emulation; physical hardware has not
  been tested.
- The ATA device is QEMU's emulated disk; physical HP G42-215BR hardware is
  unavailable for identification and testing.
- The PS/2 keyboard and mouse are QEMU-emulated devices; the notebook's
  touchpad and actual controller models have not been identified.
- ATA writes are polled to command completion, but the driver does not issue
  an ATA cache-flush command; power-loss durability is not guaranteed.
- `shutdown` halts the CPU; ACPI power-off support is not implemented.
