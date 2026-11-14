#include <stdint.h>

#include "ata.h"
#include "fat32.h"

#define SECTOR_SIZE 512u
#define FAT32_PARTITION 0x0bu
#define FAT32_LBA_PARTITION 0x0cu
#define FAT32_END_OF_CHAIN 0x0ffffff8u
#define FAT32_END_MARKER 0x0fffffffu
#define FAT32_UNKNOWN 0xffffffffu
#define DIRECTORY_ENTRY_SIZE 32u
#define ENTRIES_PER_SECTOR (SECTOR_SIZE / DIRECTORY_ENTRY_SIZE)

struct fat32_volume {
    uint32_t start_sector;
    uint32_t sector_count;
    uint32_t fat_start;
    uint32_t fat_sectors;
    uint32_t data_start;
    uint32_t cluster_count;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t fsinfo_sector;
    uint32_t backup_sector;
    uint32_t free_cluster_hint;
    uint32_t next_cluster_hint;
};

struct directory_location {
    uint32_t sector;
    uint16_t offset;
    uint8_t raw[DIRECTORY_ENTRY_SIZE];
};

static struct fat32_volume volume;
static uint8_t sector_buffer[SECTOR_SIZE];
static int mounted;

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void write_u32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void fill_bytes(uint8_t *data, uint8_t value, uint32_t size)
{
    for (uint32_t index = 0; index < size; ++index) {
        data[index] = value;
    }
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t size)
{
    for (uint32_t index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
}

static int equal_bytes(const uint8_t *left, const uint8_t *right,
                      uint32_t size)
{
    for (uint32_t index = 0; index < size; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

static int is_valid_cluster(uint32_t cluster)
{
    return cluster >= 2u && cluster < volume.cluster_count + 2u;
}

static uint32_t cluster_sector(uint32_t cluster)
{
    return volume.data_start +
           (cluster - 2u) * volume.sectors_per_cluster;
}

static int make_short_name(const char *name, uint8_t output[11])
{
    fill_bytes(output, ' ', 11u);
    if (name == 0 || name[0] == '\0') {
        return 0;
    }
    if (name[0] == '.' && (name[1] == '\0' ||
                           (name[1] == '.' && name[2] == '\0'))) {
        output[0] = '.';
        if (name[1] == '.') {
            output[1] = '.';
        }
        return 1;
    }

    uint32_t base_length = 0;
    uint32_t extension_length = 0;
    int in_extension = 0;
    for (uint32_t index = 0; name[index] != '\0'; ++index) {
        uint8_t character = (uint8_t)name[index];
        if (character == '.') {
            if (in_extension || base_length == 0u) {
                return 0;
            }
            in_extension = 1;
            continue;
        }
        if (character <= ' ' || character >= 0x7fu ||
            character == '/' || character == '\\' || character == ':' ||
            character == '*' || character == '?' || character == '"' ||
            character == '<' || character == '>' || character == '|') {
            return 0;
        }

        if (in_extension) {
            if (extension_length == 3u) {
                return 0;
            }
            output[8u + extension_length++] =
                character >= 'a' && character <= 'z'
                    ? (uint8_t)(character - 'a' + 'A')
                    : character;
        } else {
            if (base_length == 8u) {
                return 0;
            }
            output[base_length++] =
                character >= 'a' && character <= 'z'
                    ? (uint8_t)(character - 'a' + 'A')
                    : character;
        }
    }
    return base_length != 0u;
}

static void format_short_name(const uint8_t input[11], char output[13])
{
    uint32_t output_index = 0;
    for (uint32_t index = 0; index < 8u && input[index] != ' '; ++index) {
        output[output_index++] = (char)input[index];
    }

    if (input[8] != ' ') {
        output[output_index++] = '.';
        for (uint32_t index = 8; index < 11u && input[index] != ' '; ++index) {
            output[output_index++] = (char)input[index];
        }
    }
    output[output_index] = '\0';
}

static int fat_read(uint32_t cluster, uint32_t *value)
{
    if (!is_valid_cluster(cluster) || value == 0) {
        return 0;
    }

    uint32_t offset = cluster * 4u;
    uint32_t sector = volume.fat_start + offset / SECTOR_SIZE;
    if (!ata_read_sector(sector, sector_buffer)) {
        return 0;
    }
    *value = read_u32(&sector_buffer[offset % SECTOR_SIZE]) & 0x0fffffffu;
    return 1;
}

static int fat_write(uint32_t cluster, uint32_t value)
{
    if (!is_valid_cluster(cluster)) {
        return 0;
    }

    uint32_t offset = cluster * 4u;
    uint32_t sector = volume.fat_start + offset / SECTOR_SIZE;
    if (!ata_read_sector(sector, sector_buffer)) {
        return 0;
    }
    value = (read_u32(&sector_buffer[offset % SECTOR_SIZE]) & 0xf0000000u) |
            (value & 0x0fffffffu);
    write_u32(&sector_buffer[offset % SECTOR_SIZE], value);

    for (uint32_t copy = 0; copy < volume.fat_count; ++copy) {
        if (!ata_write_sector(sector + copy * volume.fat_sectors,
                              sector_buffer)) {
            return 0;
        }
    }
    return 1;
}

static int update_fsinfo(void)
{
    if (volume.fsinfo_sector == 0u ||
        volume.fsinfo_sector >= volume.reserved_sectors) {
        return 1;
    }

    uint32_t sectors[2] = {
        volume.start_sector + volume.fsinfo_sector,
        volume.start_sector + volume.backup_sector + volume.fsinfo_sector,
    };
    uint32_t copies = volume.backup_sector != 0u &&
                              volume.backup_sector != 0xffffu &&
                              volume.backup_sector + volume.fsinfo_sector <
                                  volume.reserved_sectors
                          ? 2u
                          : 1u;
    for (uint32_t index = 0; index < copies; ++index) {
        if (!ata_read_sector(sectors[index], sector_buffer)) {
            return 0;
        }
        if (read_u32(&sector_buffer[0]) != 0x41615252u ||
            read_u32(&sector_buffer[484]) != 0x61417272u) {
            return 0;
        }
        write_u32(&sector_buffer[488], volume.free_cluster_hint);
        write_u32(&sector_buffer[492], volume.next_cluster_hint);
        if (!ata_write_sector(sectors[index], sector_buffer)) {
            return 0;
        }
    }
    return 1;
}

static int allocate_cluster(uint32_t *allocated)
{
    if (allocated == 0 || volume.cluster_count == 0u) {
        return 0;
    }

    uint32_t start = volume.next_cluster_hint;
    if (!is_valid_cluster(start)) {
        start = 2u;
    }
    for (uint32_t offset = 0; offset < volume.cluster_count; ++offset) {
        uint32_t cluster =
            2u + ((start - 2u + offset) % volume.cluster_count);
        uint32_t value;
        if (!fat_read(cluster, &value)) {
            return 0;
        }
        if (value != 0u) {
            continue;
        }
        if (!fat_write(cluster, FAT32_END_MARKER)) {
            return 0;
        }

        if (volume.free_cluster_hint != FAT32_UNKNOWN &&
            volume.free_cluster_hint != 0u) {
            --volume.free_cluster_hint;
        }
        volume.next_cluster_hint =
            cluster + 1u < volume.cluster_count + 2u ? cluster + 1u : 2u;
        if (!update_fsinfo()) {
            fat_write(cluster, 0u);
            if (volume.free_cluster_hint != FAT32_UNKNOWN) {
                ++volume.free_cluster_hint;
            }
            return 0;
        }
        *allocated = cluster;
        return 1;
    }
    return 0;
}

static int release_chain(uint32_t first_cluster)
{
    uint32_t cluster = first_cluster;
    for (uint32_t count = 0; count < volume.cluster_count; ++count) {
        uint32_t next;
        if (!is_valid_cluster(cluster) || !fat_read(cluster, &next) ||
            !fat_write(cluster, 0u)) {
            return 0;
        }
        if (volume.free_cluster_hint != FAT32_UNKNOWN) {
            ++volume.free_cluster_hint;
        }
        if (next >= FAT32_END_OF_CHAIN) {
            volume.next_cluster_hint = cluster;
            return update_fsinfo();
        }
        cluster = next;
    }
    return 0;
}

static void decode_entry(const uint8_t raw[DIRECTORY_ENTRY_SIZE],
                         struct fat32_entry *entry)
{
    format_short_name(raw, entry->name);
    entry->attributes = raw[11];
    entry->first_cluster =
        ((uint32_t)read_u16(&raw[20]) << 16) | read_u16(&raw[26]);
    entry->first_cluster &= 0x0fffffffu;
    entry->size = read_u32(&raw[28]);
}

static int find_entry(uint32_t directory, const uint8_t short_name[11],
                      struct fat32_entry *entry,
                      struct directory_location *location)
{
    if (!is_valid_cluster(directory)) {
        return -1;
    }

    uint32_t cluster = directory;
    for (uint32_t chain = 0; chain < volume.cluster_count; ++chain) {
        uint32_t first_sector = cluster_sector(cluster);
        for (uint32_t sector_index = 0;
             sector_index < volume.sectors_per_cluster; ++sector_index) {
            uint32_t lba = first_sector + sector_index;
            if (!ata_read_sector(lba, sector_buffer)) {
                return -1;
            }
            for (uint32_t index = 0; index < ENTRIES_PER_SECTOR; ++index) {
                uint32_t offset = index * DIRECTORY_ENTRY_SIZE;
                uint8_t first = sector_buffer[offset];
                if (first == 0u) {
                    return 0;
                }
                if (first == 0xe5u || sector_buffer[offset + 11u] == 0x0fu ||
                    (sector_buffer[offset + 11u] & 0x08u) != 0u) {
                    continue;
                }
                if (equal_bytes(&sector_buffer[offset], short_name, 11u)) {
                    if (entry != 0) {
                        decode_entry(&sector_buffer[offset], entry);
                    }
                    if (location != 0) {
                        location->sector = lba;
                        location->offset = (uint16_t)offset;
                        copy_bytes(location->raw, &sector_buffer[offset],
                                   DIRECTORY_ENTRY_SIZE);
                    }
                    return 1;
                }
            }
        }

        uint32_t next;
        if (!fat_read(cluster, &next)) {
            return -1;
        }
        if (next >= FAT32_END_OF_CHAIN) {
            return 0;
        }
        cluster = next;
    }
    return -1;
}

static int find_free_slot(uint32_t directory, struct directory_location *location)
{
    if (!is_valid_cluster(directory) || location == 0) {
        return 0;
    }

    uint32_t cluster = directory;
    for (uint32_t chain = 0; chain < volume.cluster_count; ++chain) {
        uint32_t first_sector = cluster_sector(cluster);
        for (uint32_t sector_index = 0;
             sector_index < volume.sectors_per_cluster; ++sector_index) {
            uint32_t lba = first_sector + sector_index;
            if (!ata_read_sector(lba, sector_buffer)) {
                return 0;
            }
            for (uint32_t index = 0; index < ENTRIES_PER_SECTOR; ++index) {
                uint32_t offset = index * DIRECTORY_ENTRY_SIZE;
                uint8_t first = sector_buffer[offset];
                if (first == 0u || first == 0xe5u) {
                    location->sector = lba;
                    location->offset = (uint16_t)offset;
                    return 1;
                }
            }
        }

        uint32_t next;
        if (!fat_read(cluster, &next)) {
            return 0;
        }
        if (next >= FAT32_END_OF_CHAIN) {
            uint32_t extended;
            if (!allocate_cluster(&extended)) {
                return 0;
            }
            if (!fat_write(cluster, extended)) {
                release_chain(extended);
                return 0;
            }
            uint8_t empty_sector[SECTOR_SIZE];
            fill_bytes(empty_sector, 0u, SECTOR_SIZE);
            for (uint32_t index = 0; index < volume.sectors_per_cluster; ++index) {
                if (!ata_write_sector(cluster_sector(extended) + index,
                                      empty_sector)) {
                    fat_write(cluster, FAT32_END_MARKER);
                    release_chain(extended);
                    return 0;
                }
            }
            location->sector = cluster_sector(extended);
            location->offset = 0;
            return 1;
        }
        cluster = next;
    }
    return 0;
}

static int write_directory_entry(uint32_t directory,
                                 const uint8_t raw[DIRECTORY_ENTRY_SIZE])
{
    struct directory_location location;
    if (!find_free_slot(directory, &location) ||
        !ata_read_sector(location.sector, sector_buffer)) {
        return 0;
    }
    copy_bytes(&sector_buffer[location.offset], raw, DIRECTORY_ENTRY_SIZE);
    return ata_write_sector(location.sector, sector_buffer);
}

static void encode_entry(uint8_t raw[DIRECTORY_ENTRY_SIZE],
                         const uint8_t short_name[11], uint8_t attributes,
                         uint32_t first_cluster, uint32_t size)
{
    fill_bytes(raw, 0u, DIRECTORY_ENTRY_SIZE);
    copy_bytes(raw, short_name, 11u);
    raw[11] = attributes;
    raw[20] = (uint8_t)(first_cluster >> 16);
    raw[21] = (uint8_t)(first_cluster >> 24);
    raw[26] = (uint8_t)first_cluster;
    raw[27] = (uint8_t)(first_cluster >> 8);
    write_u32(&raw[28], size);
}

static int write_file_data(uint32_t first_cluster, const uint8_t *contents,
                           uint32_t size)
{
    uint32_t remaining = size;
    uint32_t source_offset = 0;
    uint32_t cluster = first_cluster;
    uint32_t cluster_bytes = volume.sectors_per_cluster * SECTOR_SIZE;
    uint32_t cluster_count = (size - 1u) / cluster_bytes + 1u;

    for (uint32_t current = 0; current < cluster_count; ++current) {
        if (!is_valid_cluster(cluster)) {
            return 0;
        }
        uint32_t first_sector = cluster_sector(cluster);
        for (uint32_t sector_index = 0;
             sector_index < volume.sectors_per_cluster; ++sector_index) {
            fill_bytes(sector_buffer, 0u, SECTOR_SIZE);
            uint32_t bytes = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
            copy_bytes(sector_buffer, &contents[source_offset], bytes);
            if (!ata_write_sector(first_sector + sector_index, sector_buffer)) {
                return 0;
            }
            source_offset += bytes;
            remaining -= bytes;
            if (remaining == 0u) {
                break;
            }
        }

        if (current + 1u < cluster_count) {
            uint32_t next;
            if (!fat_read(cluster, &next) || next >= FAT32_END_OF_CHAIN) {
                return 0;
            }
            cluster = next;
        }
    }
    return remaining == 0u;
}

int fat32_mount(void)
{
    uint8_t master_boot_record[SECTOR_SIZE];
    uint32_t partition_start = 0;
    uint32_t partition_size = 0;
    if (!ata_read_sector(0u, master_boot_record) ||
        master_boot_record[510] != 0x55u ||
        master_boot_record[511] != 0xaau) {
        return 0;
    }

    for (uint32_t index = 0; index < 4u; ++index) {
        uint32_t offset = 446u + index * 16u;
        uint8_t type = master_boot_record[offset + 4u];
        if (type == FAT32_PARTITION || type == FAT32_LBA_PARTITION) {
            partition_start = read_u32(&master_boot_record[offset + 8u]);
            partition_size = read_u32(&master_boot_record[offset + 12u]);
            break;
        }
    }
    if (partition_start == 0u || partition_size == 0u ||
        !ata_read_sector(partition_start, sector_buffer) ||
        sector_buffer[510] != 0x55u || sector_buffer[511] != 0xaau) {
        return 0;
    }

    uint32_t bytes_per_sector = read_u16(&sector_buffer[11]);
    uint32_t sectors_per_cluster = sector_buffer[13];
    uint32_t reserved_sectors = read_u16(&sector_buffer[14]);
    uint32_t fat_count = sector_buffer[16];
    uint32_t total_sectors = read_u16(&sector_buffer[19]);
    if (total_sectors == 0u) {
        total_sectors = read_u32(&sector_buffer[32]);
    }
    uint32_t fat_sectors = read_u32(&sector_buffer[36]);
    uint32_t root_cluster = read_u32(&sector_buffer[44]) & 0x0fffffffu;
    uint32_t fsinfo_sector = read_u16(&sector_buffer[48]);
    uint32_t backup_sector = read_u16(&sector_buffer[50]);

    if (bytes_per_sector != SECTOR_SIZE || sectors_per_cluster == 0u ||
        sectors_per_cluster > 64u ||
        (sectors_per_cluster & (sectors_per_cluster - 1u)) != 0u ||
        reserved_sectors == 0u || fat_count == 0u || fat_count > 2u ||
        fat_sectors == 0u || total_sectors == 0u ||
        total_sectors > partition_size ||
        reserved_sectors + fat_count * fat_sectors >= total_sectors) {
        return 0;
    }

    volume.start_sector = partition_start;
    volume.sector_count = total_sectors;
    volume.fat_start = partition_start + reserved_sectors;
    volume.fat_sectors = fat_sectors;
    volume.data_start = partition_start + reserved_sectors +
                        fat_count * fat_sectors;
    volume.cluster_count =
        (total_sectors - reserved_sectors - fat_count * fat_sectors) /
        sectors_per_cluster;
    volume.root_cluster = root_cluster;
    volume.sectors_per_cluster = sectors_per_cluster;
    volume.reserved_sectors = reserved_sectors;
    volume.fat_count = fat_count;
    volume.fsinfo_sector = fsinfo_sector;
    volume.backup_sector = backup_sector;
    volume.free_cluster_hint = FAT32_UNKNOWN;
    volume.next_cluster_hint = 2u;

    if (volume.cluster_count < 65525u || !is_valid_cluster(root_cluster)) {
        return 0;
    }
    if ((volume.cluster_count + 2u) * sizeof(uint32_t) >
        fat_sectors * SECTOR_SIZE) {
        return 0;
    }
    if (fsinfo_sector != 0u && fsinfo_sector < reserved_sectors &&
        ata_read_sector(partition_start + fsinfo_sector, sector_buffer) &&
        read_u32(&sector_buffer[0]) == 0x41615252u &&
        read_u32(&sector_buffer[484]) == 0x61417272u) {
        uint32_t free_count = read_u32(&sector_buffer[488]);
        uint32_t next_cluster = read_u32(&sector_buffer[492]);
        if (free_count <= volume.cluster_count) {
            volume.free_cluster_hint = free_count;
        }
        if (is_valid_cluster(next_cluster)) {
            volume.next_cluster_hint = next_cluster;
        }
    }

    mounted = 1;
    return 1;
}

uint32_t fat32_root_cluster(void)
{
    return mounted ? volume.root_cluster : 0u;
}

int fat32_find(uint32_t directory, const char *name,
               struct fat32_entry *entry)
{
    uint8_t short_name[11];
    if (!mounted || entry == 0 || !make_short_name(name, short_name)) {
        return 0;
    }
    return find_entry(directory, short_name, entry, 0) == 1;
}

int fat32_list(uint32_t directory, struct fat32_entry *entries,
               uint32_t capacity, uint32_t *count)
{
    if (!mounted || !is_valid_cluster(directory) ||
        (capacity != 0u && entries == 0) || count == 0) {
        return 0;
    }

    *count = 0;
    uint32_t cluster = directory;
    for (uint32_t chain = 0; chain < volume.cluster_count; ++chain) {
        uint32_t first_sector = cluster_sector(cluster);
        for (uint32_t sector_index = 0;
             sector_index < volume.sectors_per_cluster; ++sector_index) {
            if (!ata_read_sector(first_sector + sector_index, sector_buffer)) {
                return 0;
            }
            for (uint32_t index = 0; index < ENTRIES_PER_SECTOR; ++index) {
                uint32_t offset = index * DIRECTORY_ENTRY_SIZE;
                uint8_t first = sector_buffer[offset];
                if (first == 0u) {
                    return 1;
                }
                if (first == 0xe5u || sector_buffer[offset + 11u] == 0x0fu ||
                    (sector_buffer[offset + 11u] & 0x08u) != 0u) {
                    continue;
                }
                if (*count == capacity) {
                    return 0;
                }
                decode_entry(&sector_buffer[offset], &entries[*count]);
                ++*count;
            }
        }

        uint32_t next;
        if (!fat_read(cluster, &next)) {
            return 0;
        }
        if (next >= FAT32_END_OF_CHAIN) {
            return 1;
        }
        cluster = next;
    }
    return 0;
}

int fat32_read_file(uint32_t directory, const char *name, void *buffer,
                    uint32_t capacity, uint32_t *size)
{
    struct fat32_entry entry;
    if (size == 0 || !fat32_find(directory, name, &entry) ||
        (entry.attributes & FAT32_ATTRIBUTE_DIRECTORY) != 0u ||
        entry.size > capacity || (entry.size != 0u && buffer == 0)) {
        return 0;
    }
    *size = entry.size;
    if (entry.size == 0u) {
        return 1;
    }

    uint8_t *destination = (uint8_t *)buffer;
    uint32_t remaining = entry.size;
    uint32_t cluster = entry.first_cluster;
    uint32_t cluster_bytes = volume.sectors_per_cluster * SECTOR_SIZE;
    uint32_t cluster_count = (entry.size - 1u) / cluster_bytes + 1u;
    for (uint32_t current = 0; current < cluster_count; ++current) {
        if (!is_valid_cluster(cluster)) {
            return 0;
        }
        uint32_t first_sector = cluster_sector(cluster);
        for (uint32_t sector_index = 0;
             sector_index < volume.sectors_per_cluster; ++sector_index) {
            if (!ata_read_sector(first_sector + sector_index, sector_buffer)) {
                return 0;
            }
            uint32_t bytes = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;
            copy_bytes(&destination[entry.size - remaining], sector_buffer, bytes);
            remaining -= bytes;
            if (remaining == 0u) {
                break;
            }
        }
        if (current + 1u < cluster_count) {
            uint32_t next;
            if (!fat_read(cluster, &next) || next >= FAT32_END_OF_CHAIN) {
                return 0;
            }
            cluster = next;
        }
    }
    return remaining == 0u;
}

int fat32_write_file(uint32_t directory, const char *name, const void *buffer,
                     uint32_t size)
{
    uint8_t short_name[11];
    struct fat32_entry existing;
    if (!mounted || !is_valid_cluster(directory) ||
        !make_short_name(name, short_name) ||
        (size != 0u && buffer == 0) ||
        find_entry(directory, short_name, &existing, 0) != 0) {
        return 0;
    }

    uint32_t first_cluster = 0;
    uint32_t cluster_bytes = volume.sectors_per_cluster * SECTOR_SIZE;
    uint32_t clusters_needed =
        size == 0u ? 0u : (size - 1u) / cluster_bytes + 1u;
    uint32_t previous = 0;
    for (uint32_t index = 0; index < clusters_needed; ++index) {
        uint32_t cluster;
        if (!allocate_cluster(&cluster)) {
            if (first_cluster != 0u) {
                release_chain(first_cluster);
            }
            return 0;
        }
        if (first_cluster == 0u) {
            first_cluster = cluster;
        } else if (!fat_write(previous, cluster)) {
            fat_write(cluster, 0u);
            release_chain(first_cluster);
            return 0;
        }
        previous = cluster;
    }

    if (size != 0u &&
        !write_file_data(first_cluster, (const uint8_t *)buffer, size)) {
        if (first_cluster != 0u) {
            release_chain(first_cluster);
        }
        return 0;
    }

    uint8_t raw[DIRECTORY_ENTRY_SIZE];
    encode_entry(raw, short_name, 0x20u, first_cluster, size);
    if (!write_directory_entry(directory, raw)) {
        if (first_cluster != 0u) {
            release_chain(first_cluster);
        }
        return 0;
    }
    return 1;
}

int fat32_create_directory(uint32_t parent, const char *name,
                           uint32_t *directory_cluster)
{
    uint8_t short_name[11];
    struct fat32_entry existing;
    if (!mounted || !is_valid_cluster(parent) || directory_cluster == 0 ||
        !make_short_name(name, short_name) ||
        find_entry(parent, short_name, &existing, 0) != 0) {
        return 0;
    }

    uint32_t cluster;
    if (!allocate_cluster(&cluster)) {
        return 0;
    }
    uint8_t empty_sector[SECTOR_SIZE];
    fill_bytes(empty_sector, 0u, SECTOR_SIZE);
    for (uint32_t index = 0; index < volume.sectors_per_cluster; ++index) {
        if (!ata_write_sector(cluster_sector(cluster) + index, empty_sector)) {
            release_chain(cluster);
            return 0;
        }
    }

    uint8_t dot[DIRECTORY_ENTRY_SIZE];
    uint8_t dot_dot[DIRECTORY_ENTRY_SIZE];
    uint8_t dot_name[11];
    uint8_t dot_dot_name[11];
    fill_bytes(dot_name, ' ', sizeof(dot_name));
    fill_bytes(dot_dot_name, ' ', sizeof(dot_dot_name));
    dot_name[0] = '.';
    dot_dot_name[0] = '.';
    dot_dot_name[1] = '.';
    encode_entry(dot, dot_name, FAT32_ATTRIBUTE_DIRECTORY, cluster, 0u);
    encode_entry(dot_dot, dot_dot_name, FAT32_ATTRIBUTE_DIRECTORY, parent, 0u);
    copy_bytes(empty_sector, dot, sizeof(dot));
    copy_bytes(&empty_sector[DIRECTORY_ENTRY_SIZE], dot_dot,
               sizeof(dot_dot));
    if (!ata_write_sector(cluster_sector(cluster), empty_sector)) {
        release_chain(cluster);
        return 0;
    }

    uint8_t raw[DIRECTORY_ENTRY_SIZE];
    encode_entry(raw, short_name, FAT32_ATTRIBUTE_DIRECTORY, cluster, 0u);
    if (!write_directory_entry(parent, raw)) {
        release_chain(cluster);
        return 0;
    }
    *directory_cluster = cluster;
    return 1;
}

int fat32_delete(uint32_t directory, const char *name)
{
    uint8_t short_name[11];
    struct fat32_entry entry;
    struct directory_location location;
    if (!mounted || !is_valid_cluster(directory) ||
        !make_short_name(name, short_name) ||
        (short_name[0] == '.' &&
         (short_name[1] == ' ' || short_name[1] == '.') &&
         (short_name[1] == ' ' || short_name[2] == ' ')) ||
        find_entry(directory, short_name, &entry, &location) != 1 ||
        (entry.attributes & 0x08u) != 0u) {
        return 0;
    }

    if ((entry.attributes & FAT32_ATTRIBUTE_DIRECTORY) != 0u) {
        if (entry.first_cluster == volume.root_cluster ||
            !is_valid_cluster(entry.first_cluster)) {
            return 0;
        }
        struct fat32_entry children[2];
        uint32_t child_count;
        if (!fat32_list(entry.first_cluster, children, 2u, &child_count)) {
            return 0;
        }
        for (uint32_t index = 0; index < child_count; ++index) {
            if (children[index].name[0] != '.' ||
                (children[index].name[1] != '\0' &&
                 !(children[index].name[1] == '.' &&
                   children[index].name[2] == '\0'))) {
                return 0;
            }
        }
    }

    if (!ata_read_sector(location.sector, sector_buffer)) {
        return 0;
    }
    sector_buffer[location.offset] = 0xe5u;
    if (!ata_write_sector(location.sector, sector_buffer)) {
        return 0;
    }
    if (entry.first_cluster >= 2u && !release_chain(entry.first_cluster)) {
        return 0;
    }
    return 1;
}
