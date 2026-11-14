# TexugOS

TexugOS is an experimental x86 operating system being developed in stages. The
current milestone is the Legacy BIOS boot path.

## Implemented: Stages 1-3

The boot image contains a 512-byte boot sector followed by a fixed 32-sector
payload. The boot sector checks BIOS LBA-extension support, loads the payload
at physical address `0x10000`, and transfers control to it. The payload collects
the BIOS E820 memory map, enables A20, enters 32-bit protected mode, and calls a
freestanding C entry point. The entry point reports usable RAM, initializes
and enables a 16 MiB identity-mapped paging region, installs exception and IRQ
handlers in the IDT, remaps the 8259 PIC, and starts the PIT at 100 Hz. Timer
interrupts are acknowledged and counted. A physical-frame bitmap is populated
from E820, the kernel has a small coalescing heap, and a cooperative scheduler
can switch between kernel threads with separate stacks.

The image is a raw, unpartitioned 16 MiB BIOS disk image. It is not an ISO and
is not yet an installer.

### Build and test

Required tools: GNU Make, GCC with 32-bit compilation support, GNU binutils,
Python 3, and QEMU's `qemu-system-i386` for runtime testing.

```sh
make
make test
make run
```

`make test` builds the image, checks its boot-sector signature and fixed load
layout, and runs a headless QEMU smoke test that verifies the kernel's VGA
status output. `make run` starts QEMU with the raw disk image.
The HP G42-215BR hardware has not been inspected; no device-specific drivers
are claimed or included.

## Current status and limitations

- Implemented: BIOS boot sector, LBA disk read, E820 handoff, A20 enable,
  protected-mode transition, initial 16 MiB paging, CPU exceptions, PIC IRQ
  handling, 100 Hz PIT timer, E820-backed frame allocation, kernel heap, and
  cooperative kernel threads.
- Not yet implemented: user-mode process isolation, preemptive scheduling,
  storage/filesystem drivers, shell, graphics desktop, networking, or apps.
- The QEMU smoke test confirms boot in emulation; physical hardware has not
  been tested.
- Physical HP G42-215BR hardware is unavailable for identification and testing.
