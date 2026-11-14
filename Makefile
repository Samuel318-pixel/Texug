CC := gcc
AS := as
LD := ld
OBJCOPY := objcopy
PYTHON := python3
BUILD := build

CFLAGS := -m32 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding \
	-fno-pie -fno-pic -fno-stack-protector -fno-builtin
CPPFLAGS := -Ikernel -Imemory -Idrivers -Ifilesystem -Ishell -Idesktop
ASFLAGS := --32
LDFLAGS := -m elf_i386 -nostdlib

.PHONY: all clean test run

all: $(BUILD)/texugos.img

$(BUILD):
	mkdir -p $@

$(BUILD)/boot.o: boot/boot.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/boot.elf: $(BUILD)/boot.o boot/boot.ld
	$(LD) $(LDFLAGS) -T boot/boot.ld -o $@ $<

$(BUILD)/boot.bin: $(BUILD)/boot.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/stage2.o: boot/stage2.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/kernel.o: kernel/kernel.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: kernel/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/paging.o: memory/paging.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/frame_allocator.o: memory/frame_allocator.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/heap.o: memory/heap.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/pic.o: drivers/pic.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/pit.o: drivers/pit.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/keyboard.o: drivers/keyboard.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/mouse.o: drivers/mouse.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/pci.o: drivers/pci.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/video.o: drivers/video.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/ata.o: drivers/ata.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/fat32.o: filesystem/fat32.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/shell.o: shell/shell.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/desktop.o: desktop/desktop.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/interrupt_stubs.o: kernel/interrupt_stubs.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/context_switch.o: kernel/context_switch.S | $(BUILD)
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD)/payload.elf: $(BUILD)/stage2.o $(BUILD)/interrupt_stubs.o \
		$(BUILD)/context_switch.o $(BUILD)/kernel.o $(BUILD)/console.o \
		$(BUILD)/interrupts.o $(BUILD)/threads.o $(BUILD)/paging.o \
		$(BUILD)/frame_allocator.o $(BUILD)/heap.o \
		$(BUILD)/pic.o $(BUILD)/pit.o $(BUILD)/keyboard.o $(BUILD)/mouse.o \
		$(BUILD)/pci.o $(BUILD)/video.o $(BUILD)/desktop.o $(BUILD)/ata.o \
		$(BUILD)/fat32.o $(BUILD)/shell.o \
		boot/payload.ld
	$(LD) $(LDFLAGS) -T boot/payload.ld -o $@ $(filter %.o,$^)

$(BUILD)/payload.bin: $(BUILD)/payload.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/texugos.img: $(BUILD)/boot.bin $(BUILD)/payload.bin tools/mkimage.py
	$(PYTHON) tools/mkimage.py $(BUILD)/boot.bin $(BUILD)/payload.bin $@

test: all
	$(PYTHON) -m unittest discover -s tests -v

run: all
	qemu-system-i386 -drive file=$(BUILD)/texugos.img,format=raw,if=ide -boot c

clean:
	rm -rf $(BUILD)
