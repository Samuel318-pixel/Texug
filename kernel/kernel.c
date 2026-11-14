#include <stdint.h>

#include "console.h"
#include "interrupts.h"
#include "frame_allocator.h"
#include "heap.h"
#include "paging.h"
#include "pic.h"
#include "pit.h"
#include "threads.h"
#include "ata.h"
#include "fat32.h"

#define E820_BUFFER ((volatile const struct e820_entry *)0x5000u)
#define E820_MAX_ENTRIES 32u
#define BOOT_MAGIC 0x54455847u

static volatile uint32_t thread_a_runs;
static volatile uint32_t thread_b_runs;

static int filesystem_self_test(void)
{
    uint32_t root = fat32_root_cluster();
    uint32_t directory;
    uint8_t expected[700];
    uint8_t actual[700];
    for (uint32_t index = 0; index < sizeof(expected); ++index) {
        expected[index] = (uint8_t)('A' + index % 26u);
    }

    if (root == 0u || !fat32_create_directory(root, "KERNEL", &directory)) {
        console_write("FAT32 mkdir self-test failed.\n");
        return 0;
    }
    if (!fat32_write_file(directory, "BOOT.BIN", expected, sizeof(expected))) {
        console_write("FAT32 file write self-test failed.\n");
        return 0;
    }

    uint32_t size = 0;
    if (!fat32_read_file(directory, "BOOT.BIN", actual, sizeof(actual), &size) ||
        size != sizeof(expected)) {
        console_write("FAT32 file read self-test failed.\n");
        return 0;
    }
    for (uint32_t index = 0; index < size; ++index) {
        if (actual[index] != expected[index]) {
            console_write("FAT32 data comparison failed.\n");
            return 0;
        }
    }
    if (!fat32_delete(directory, "BOOT.BIN")) {
        console_write("FAT32 file delete self-test failed.\n");
        return 0;
    }
    if (!fat32_delete(root, "KERNEL")) {
        console_write("FAT32 directory delete self-test failed.\n");
        return 0;
    }
    return 1;
}

static int memory_self_test(void)
{
    uint32_t frames_before = frame_free_count();
    uint32_t first_frame = frame_allocate();
    uint32_t second_frame = frame_allocate();
    if (first_frame == FRAME_INVALID || second_frame == FRAME_INVALID ||
        first_frame == second_frame) {
        return 0;
    }

    if (first_frame < 0x01000000u) {
        volatile uint32_t *frame = (volatile uint32_t *)(uintptr_t)first_frame;
        *frame = 0x54455847u;
        if (*frame != 0x54455847u) {
            return 0;
        }
    }

    if (!frame_free(first_frame) || frame_free(first_frame) ||
        !frame_free(second_frame) || frame_free_count() != frames_before) {
        return 0;
    }

    void *first = kmalloc(16u);
    void *middle = kmalloc(64u);
    void *last = kmalloc(16u);
    if (first == 0 || middle == 0 || last == 0 ||
        ((uintptr_t)first & 7u) != 0u ||
        ((uintptr_t)middle & 7u) != 0u ||
        ((uintptr_t)last & 7u) != 0u) {
        return 0;
    }
    if (!kfree(middle) || kfree(middle)) {
        return 0;
    }
    void *reused = kmalloc(32u);
    if (reused == 0 || kfree((uint8_t *)first + 1u)) {
        return 0;
    }
    if (!kfree(first) || !kfree(last) || !kfree(reused)) {
        return 0;
    }

    void *large = kmalloc(32u * 1024u);
    if (large == 0 || !kfree(large)) {
        return 0;
    }
    return 1;
}

static void halt_forever(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void thread_a(void *argument)
{
    (void)argument;
    for (uint32_t index = 0; index < 3u; ++index) {
        ++thread_a_runs;
        thread_yield();
    }
}

static void thread_b(void *argument)
{
    (void)argument;
    for (uint32_t index = 0; index < 3u; ++index) {
        ++thread_b_runs;
        thread_yield();
    }
}

void kernel_main(uint32_t boot_magic, uint32_t memory_map_address,
                 uint32_t memory_map_entries)
{
    console_clear();
    console_write("TexugOS - BIOS boot stage\n\n");

    if (boot_magic != BOOT_MAGIC || memory_map_address != 0x5000u ||
        memory_map_entries == 0u || memory_map_entries > E820_MAX_ENTRIES) {
        console_write("Boot handoff validation failed.\n");
        halt_forever();
    }

    uint64_t usable_bytes = 0;
    for (uint32_t index = 0; index < memory_map_entries; ++index) {
        if (E820_BUFFER[index].type == 1u) {
            usable_bytes += E820_BUFFER[index].length;
        }
    }

    console_write("BIOS E820 entries: ");
    console_write_u32(memory_map_entries);
    console_write("\nUsable RAM: ");
    console_write_u32((uint32_t)(usable_bytes >> 20));
    console_write(" MiB\n");
    for (uint32_t index = 0; index < memory_map_entries; ++index) {
        console_write("E820 ");
        console_write_u32(index);
        console_write(" type ");
        console_write_u32(E820_BUFFER[index].type);
        console_write(" length high ");
        console_write_u32((uint32_t)(E820_BUFFER[index].length >> 32));
        console_write(" low ");
        console_write_u32((uint32_t)E820_BUFFER[index].length);
        console_write("\n");
    }

    paging_initialize();
    console_write("Paging: 16 MiB identity map active\n");

    frame_allocator_initialize(E820_BUFFER, memory_map_entries);
    console_write("Memory: frame map initialized\n");
    heap_initialize();
    console_write("Memory: heap initialized\n");
    if (!memory_self_test()) {
        console_write("Memory allocator initialization failed.\n");
        halt_forever();
    }
    console_write("Memory: frame allocator and kernel heap active\n");

    if (!ata_initialize()) {
        console_write("ATA primary-master device not detected.\n");
        halt_forever();
    }
    console_write("ATA: ");
    console_write(ata_model());
    console_write("\n");
    if (!fat32_mount()) {
        console_write("FAT32 mount failed.\n");
        halt_forever();
    }
    if (!filesystem_self_test()) {
        console_write("FAT32 read/write test failed.\n");
        halt_forever();
    }
    console_write("FAT32: mount and CRUD test passed\n");

    interrupts_initialize();
    pic_initialize();
    pit_initialize(100u);
    console_write("IDT: exceptions and IRQ vectors installed\n");
    console_write("PIC: remapped; PIT: 100 Hz\n");

    __asm__ volatile ("sti");
    uint32_t initial_ticks = pit_ticks_read();
    while ((uint32_t)(pit_ticks_read() - initial_ticks) < 10u) {
        __asm__ volatile ("hlt");
    }

    console_write("PIT ticks: ");
    console_write_u32(pit_ticks_read());
    console_write("\n");

    if (thread_create(thread_a, 0) < 0 || thread_create(thread_b, 0) < 0) {
        console_write("Thread creation failed.\n");
        halt_forever();
    }
    uint32_t completed_threads = scheduler_run();
    if (completed_threads != 2u || thread_a_runs != 3u || thread_b_runs != 3u) {
        console_write("Thread scheduler self-test failed.\n");
        halt_forever();
    }
    console_write("Threads: ");
    console_write_u32(completed_threads);
    console_write(" cooperative tasks completed\n");
    console_write("Kernel stage 3 active.\n");

    halt_forever();
}
