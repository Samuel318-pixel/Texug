#include <stdint.h>

#include "console.h"
#include "desktop.h"
#include "fat32.h"
#include "frame_allocator.h"
#include "keyboard.h"
#include "mouse.h"
#include "pci.h"
#include "shell.h"

#define COMMAND_BUFFER_SIZE 128u
#define COMMAND_NAME_SIZE 16u
#define SHELL_PATH_SIZE 96u
#define LIST_CAPACITY 64u
#define FILE_BUFFER_SIZE 2048u

static char command_buffer[COMMAND_BUFFER_SIZE];
static char current_path[SHELL_PATH_SIZE] = "/";
static uint32_t current_directory;

static int string_equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static uint32_t string_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void copy_string(char *destination, const char *source)
{
    while (*source != '\0') {
        *destination++ = *source++;
    }
    *destination = '\0';
}

static void print_prompt(void)
{
    console_write("texugos:");
    console_write(current_path);
    console_write(" > ");
}

static void read_command_line(char *buffer, uint32_t capacity)
{
    uint32_t length = 0;
    for (;;) {
        char character;
        if (!keyboard_read_character(&character)) {
            __asm__ volatile ("sti; hlt");
            continue;
        }
        if (character == '\n') {
            console_write("\n");
            buffer[length] = '\0';
            return;
        }
        if (character == '\b') {
            if (length != 0u) {
                --length;
                console_write_character('\b');
            }
            continue;
        }
        if (character >= ' ' && character <= '~' &&
            length + 1u < capacity) {
            buffer[length++] = character;
            console_write_character(character);
        }
    }
}

static int parse_command(char *line, char command[COMMAND_NAME_SIZE],
                         char **arguments)
{
    while (*line == ' ') {
        ++line;
    }
    if (*line == '\0') {
        command[0] = '\0';
        *arguments = line;
        return 1;
    }

    uint32_t command_length = 0;
    while (*line != '\0' && *line != ' ') {
        if (command_length + 1u >= COMMAND_NAME_SIZE) {
            return 0;
        }
        command[command_length++] = *line++;
    }
    command[command_length] = '\0';
    while (*line == ' ') {
        ++line;
    }
    *arguments = line;

    uint32_t length = string_length(line);
    while (length != 0u && line[length - 1u] == ' ') {
        line[--length] = '\0';
    }
    return 1;
}

static void print_help(void)
{
    console_write("Commands: help clear echo ls cd mkdir touch cat rm\n");
    console_write("          reboot shutdown memory version processes mouse devices desktop\n");
}

static void list_directory(void)
{
    struct fat32_entry entries[LIST_CAPACITY];
    uint32_t count;
    if (!fat32_list(current_directory, entries, LIST_CAPACITY, &count)) {
        console_write("Unable to list directory (capacity: ");
        console_write_u32(LIST_CAPACITY);
        console_write(" entries).\n");
        return;
    }

    for (uint32_t index = 0; index < count; ++index) {
        console_write(entries[index].name);
        if ((entries[index].attributes & FAT32_ATTRIBUTE_DIRECTORY) != 0u) {
            console_write("/");
        } else {
            console_write("  ");
            console_write_u32(entries[index].size);
            console_write(" bytes");
        }
        console_write("\n");
    }
}

static void change_directory(const char *name)
{
    if (*name == '\0' || string_equal(name, "/")) {
        current_directory = fat32_root_cluster();
        current_path[0] = '/';
        current_path[1] = '\0';
        return;
    }
    if (string_equal(name, ".")) {
        return;
    }
    if (string_equal(name, "..") &&
        current_directory == fat32_root_cluster()) {
        return;
    }

    struct fat32_entry entry;
    if (!fat32_find(current_directory, name, &entry) ||
        (entry.attributes & FAT32_ATTRIBUTE_DIRECTORY) == 0u) {
        console_write("Directory not found.\n");
        return;
    }

    if (string_equal(name, "..")) {
        current_directory = entry.first_cluster;
        uint32_t length = string_length(current_path);
        while (length > 1u && current_path[length - 1u] != '/') {
            current_path[--length] = '\0';
        }
        if (length > 1u) {
            current_path[length - 1u] = '\0';
        }
        return;
    }

    uint32_t path_length = string_length(current_path);
    uint32_t name_length = string_length(entry.name);
    uint32_t separator = path_length == 1u ? 0u : 1u;
    if (path_length + separator + name_length >= SHELL_PATH_SIZE) {
        console_write("Path is too long.\n");
        return;
    }
    current_directory = entry.first_cluster;
    if (separator != 0u) {
        current_path[path_length++] = '/';
    }
    copy_string(&current_path[path_length], entry.name);
}

static void make_directory(const char *name)
{
    uint32_t cluster;
    if (*name != '\0' &&
        fat32_create_directory(current_directory, name, &cluster)) {
        console_write("Directory created.\n");
    } else {
        console_write("Unable to create directory.\n");
    }
}

static void create_file(const char *name)
{
    static const char empty_file[] = "";
    if (*name != '\0' &&
        fat32_write_file(current_directory, name, empty_file, 0u)) {
        console_write("File created.\n");
    } else {
        console_write("Unable to create file.\n");
    }
}

static void print_file(const char *name)
{
    char contents[FILE_BUFFER_SIZE];
    uint32_t size;
    if (*name == '\0' ||
        !fat32_read_file(current_directory, name, contents,
                         sizeof(contents), &size)) {
        console_write("Unable to read file (maximum ");
        console_write_u32(FILE_BUFFER_SIZE);
        console_write(" bytes).\n");
        return;
    }

    for (uint32_t index = 0; index < size; ++index) {
        console_write_character(contents[index]);
    }
    console_write("\n");
}

static void remove_entry(const char *name)
{
    if (*name != '\0' && fat32_delete(current_directory, name)) {
        console_write("Removed.\n");
    } else {
        console_write("Unable to remove entry.\n");
    }
}

static void list_pci_devices(void)
{
    uint32_t count = pci_device_count();
    console_write("PCI devices: ");
    console_write_u32(count);
    console_write("\n");
    if (pci_inventory_truncated()) {
        console_write("PCI inventory truncated at 64 devices.\n");
    }
    for (uint32_t index = 0; index < count; ++index) {
        struct pci_device device;
        if (!pci_get_device(index, &device)) {
            console_write("PCI inventory changed while reading.\n");
            return;
        }
        console_write("PCI ");
        console_write_u32(device.bus);
        console_write(":");
        console_write_u32(device.slot);
        console_write(".");
        console_write_u32(device.function);
        console_write(" ID ");
        console_write_hex32(((uint32_t)device.vendor_id << 16) |
                            device.device_id);
        console_write(" class ");
        console_write_hex32(((uint32_t)device.class_code << 16) |
                            ((uint32_t)device.subclass << 8) |
                            device.programming_interface);
        console_write("\n");
    }
}

static void reboot_system(void)
{
    uint8_t status;
    uint32_t attempts = 100000u;
    int reset_requested = 0;
    do {
        __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"(0x64u));
        if ((status & 0x02u) == 0u) {
            uint8_t reset = 0xfeu;
            __asm__ volatile ("outb %0, %1" : : "a"(reset), "Nd"(0x64u));
            reset_requested = 1;
            break;
        }
    } while (--attempts != 0u);

    if (reset_requested == 0) {
        console_write("Keyboard controller reset is unavailable; system halted.\n");
    }
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void shutdown_system(void)
{
    console_write("System halted; ACPI power-off is unavailable.\n");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

static void execute_command(char *line)
{
    char command[COMMAND_NAME_SIZE];
    char *arguments;
    if (!parse_command(line, command, &arguments)) {
        console_write("Command name is too long.\n");
        return;
    }

    if (string_equal(command, "")) {
        return;
    } else if (string_equal(command, "help")) {
        print_help();
    } else if (string_equal(command, "clear")) {
        console_clear();
    } else if (string_equal(command, "echo")) {
        console_write(arguments);
        console_write("\n");
    } else if (string_equal(command, "ls")) {
        list_directory();
    } else if (string_equal(command, "cd")) {
        change_directory(arguments);
    } else if (string_equal(command, "mkdir")) {
        make_directory(arguments);
    } else if (string_equal(command, "touch")) {
        create_file(arguments);
    } else if (string_equal(command, "cat")) {
        print_file(arguments);
    } else if (string_equal(command, "rm")) {
        remove_entry(arguments);
    } else if (string_equal(command, "reboot")) {
        reboot_system();
    } else if (string_equal(command, "shutdown")) {
        shutdown_system();
    } else if (string_equal(command, "memory")) {
        uint32_t free_frames = frame_free_count();
        console_write("Free physical frames: ");
        console_write_u32(free_frames);
        console_write(" (");
        console_write_u32(free_frames / 256u);
        console_write(" MiB).\n");
    } else if (string_equal(command, "version")) {
        console_write("TexugOS version 0.1.0 (32-bit BIOS).\n");
    } else if (string_equal(command, "processes")) {
        console_write("User processes: 0 (process isolation not implemented).\n");
        console_write("Kernel threads use a cooperative scheduler.\n");
    } else if (string_equal(command, "mouse")) {
        struct mouse_state state;
        mouse_get_state(&state);
        console_write("Mouse packets: ");
        console_write_u32(state.packets);
        console_write(" position ");
        console_write_u32(state.x);
        console_write(",");
        console_write_u32(state.y);
        console_write(" buttons ");
        console_write_u32(state.buttons);
        console_write("\n");
    } else if (string_equal(command, "devices")) {
        list_pci_devices();
    } else if (string_equal(command, "desktop")) {
        desktop_run();
    } else {
        console_write("Unknown command. Type 'help'.\n");
    }
}

void shell_run(void)
{
    current_directory = fat32_root_cluster();
    console_write("TexugOS shell ready. Type 'help'.\n");
    for (;;) {
        print_prompt();
        read_command_line(command_buffer, sizeof(command_buffer));
        execute_command(command_buffer);
    }
}
