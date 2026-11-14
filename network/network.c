#include <stdint.h>

#include "network.h"
#include "pit.h"
#include "rtl8139.h"

#define ETHERNET_HEADER_SIZE 14u
#define IPV4_HEADER_SIZE 20u
#define UDP_HEADER_SIZE 8u
#define DHCP_FIXED_SIZE 240u
#define DHCP_MAX_SIZE 320u
#define PACKET_CAPACITY 1600u
#define DHCP_TIMEOUT_TICKS 150u
#define PING_TIMEOUT_TICKS 200u
#define DHCP_TRANSACTION_ID 0x54584731u
#define ICMP_IDENTIFIER 0x5447u

#define ETHERNET_IPV4 0x0800u
#define ETHERNET_ARP 0x0806u
#define IPV4_UDP 17u
#define IPV4_ICMP 1u

struct dhcp_reply {
    uint8_t message_type;
    uint8_t address[4];
    uint8_t subnet_mask[4];
    uint8_t gateway[4];
    uint8_t dns_server[4];
    uint8_t server[4];
};

static struct network_config configuration;
static uint8_t hardware_address[6];
static uint8_t transmit_packet[DHCP_MAX_SIZE];
static uint8_t dhcp_payload[DHCP_MAX_SIZE];
static uint8_t receive_packet[PACKET_CAPACITY];
static uint16_t ipv4_identifier;
static uint8_t network_available;
static const char *dhcp_error;

static uint16_t read_be16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static void write_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void write_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index) {
        destination[index] = source[index];
    }
}

static int bytes_equal(const uint8_t *left, const uint8_t *right,
                       uint32_t length)
{
    for (uint32_t index = 0u; index < length; ++index) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

static uint16_t internet_checksum(const uint8_t *data, uint32_t length)
{
    uint32_t sum = 0u;
    while (length >= 2u) {
        sum += read_be16(data);
        data += 2;
        length -= 2u;
    }
    if (length != 0u) {
        sum += (uint16_t)data[0] << 8;
    }
    while ((sum >> 16) != 0u) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static void write_ethernet_header(uint8_t *frame, const uint8_t *destination,
                                  uint16_t protocol)
{
    copy_bytes(frame, destination, 6u);
    copy_bytes(frame + 6u, hardware_address, 6u);
    write_be16(frame + 12u, protocol);
}

static uint8_t *write_ipv4_header(uint8_t *frame, const uint8_t *destination,
                                  uint8_t protocol, uint16_t payload_length)
{
    uint8_t *ip = frame + ETHERNET_HEADER_SIZE;
    ip[0] = 0x45u;
    ip[1] = 0u;
    write_be16(ip + 2u, (uint16_t)(IPV4_HEADER_SIZE + payload_length));
    write_be16(ip + 4u, ++ipv4_identifier);
    write_be16(ip + 6u, 0x4000u);
    ip[8] = 64u;
    ip[9] = protocol;
    ip[10] = 0u;
    ip[11] = 0u;
    copy_bytes(ip + 12u, configuration.address, 4u);
    copy_bytes(ip + 16u, destination, 4u);
    write_be16(ip + 10u, internet_checksum(ip, IPV4_HEADER_SIZE));
    return ip;
}

static int send_udp(const uint8_t *ethernet_destination,
                    const uint8_t *ip_destination, uint16_t source_port,
                    uint16_t destination_port, const uint8_t *payload,
                    uint16_t payload_length)
{
    uint32_t frame_length = ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE +
                            UDP_HEADER_SIZE + payload_length;
    if (frame_length > sizeof(transmit_packet)) {
        return 0;
    }
    write_ethernet_header(transmit_packet, ethernet_destination,
                          ETHERNET_IPV4);
    uint8_t *ip = write_ipv4_header(
        transmit_packet, ip_destination, IPV4_UDP,
        (uint16_t)(UDP_HEADER_SIZE + payload_length));
    uint8_t *udp = ip + IPV4_HEADER_SIZE;
    write_be16(udp, source_port);
    write_be16(udp + 2u, destination_port);
    write_be16(udp + 4u, (uint16_t)(UDP_HEADER_SIZE + payload_length));
    write_be16(udp + 6u, 0u);
    copy_bytes(udp + UDP_HEADER_SIZE, payload, payload_length);
    if (frame_length < 60u) {
        for (uint32_t index = frame_length; index < 60u; ++index) {
            transmit_packet[index] = 0u;
        }
        frame_length = 60u;
    }
    return rtl8139_send(transmit_packet, (uint16_t)frame_length);
}

static uint16_t make_dhcp_message(uint8_t *payload, uint8_t message_type,
                                  const struct dhcp_reply *request)
{
    for (uint32_t index = 0u; index < DHCP_MAX_SIZE; ++index) {
        payload[index] = 0u;
    }
    payload[0] = 1u;
    payload[1] = 1u;
    payload[2] = 6u;
    write_be32(payload + 4u, DHCP_TRANSACTION_ID);
    if (message_type == 3u && request != 0) {
        copy_bytes(payload + 12u, request->address, 4u);
    }
    payload[10] = 0x80u;
    copy_bytes(payload + 28u, hardware_address, 6u);
    write_be32(payload + 236u, 0x63825363u);

    uint32_t option = DHCP_FIXED_SIZE;
    payload[option++] = 53u;
    payload[option++] = 1u;
    payload[option++] = message_type;
    if (message_type == 3u && request != 0) {
        payload[option++] = 50u;
        payload[option++] = 4u;
        copy_bytes(payload + option, request->address, 4u);
        option += 4u;
        payload[option++] = 54u;
        payload[option++] = 4u;
        copy_bytes(payload + option, request->server, 4u);
        option += 4u;
    }
    payload[option++] = 55u;
    payload[option++] = 4u;
    payload[option++] = 1u;
    payload[option++] = 3u;
    payload[option++] = 6u;
    payload[option++] = 51u;
    payload[option++] = 255u;
    return (uint16_t)option;
}

static int parse_ipv4_udp(const uint8_t *frame, uint16_t frame_length,
                          const uint8_t **ip_output,
                          const uint8_t **udp_output,
                          const uint8_t **payload_output,
                          uint16_t *payload_length)
{
    if (frame_length < ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE ||
        read_be16(frame + 12u) != ETHERNET_IPV4) {
        return 0;
    }
    const uint8_t *ip = frame + ETHERNET_HEADER_SIZE;
    uint32_t header_length = (uint32_t)(ip[0] & 0x0fu) * 4u;
    uint16_t total_length = read_be16(ip + 2u);
    if ((ip[0] >> 4) != 4u || header_length < IPV4_HEADER_SIZE ||
        total_length < header_length + UDP_HEADER_SIZE ||
        total_length > frame_length - ETHERNET_HEADER_SIZE ||
        ip[9] != IPV4_UDP) {
        return 0;
    }
    const uint8_t *udp = ip + header_length;
    uint16_t udp_length = read_be16(udp + 4u);
    if (udp_length < UDP_HEADER_SIZE ||
        udp_length > total_length - header_length) {
        return 0;
    }
    *ip_output = ip;
    *udp_output = udp;
    *payload_output = udp + UDP_HEADER_SIZE;
    *payload_length = (uint16_t)(udp_length - UDP_HEADER_SIZE);
    return 1;
}

static int parse_dhcp_reply(const uint8_t *frame, uint16_t frame_length,
                            struct dhcp_reply *reply)
{
    const uint8_t *ip;
    const uint8_t *udp;
    const uint8_t *payload;
    uint16_t payload_length;
    if (!parse_ipv4_udp(frame, frame_length, &ip, &udp, &payload,
                        &payload_length) ||
        read_be16(udp + 2u) != 68u || payload_length < DHCP_FIXED_SIZE ||
        payload[0] != 2u || payload[1] != 1u || payload[2] != 6u ||
        read_be32(payload + 4u) != DHCP_TRANSACTION_ID ||
        !bytes_equal(payload + 28u, hardware_address, 6u) ||
        read_be32(payload + 236u) != 0x63825363u) {
        return 0;
    }

    reply->message_type = 0u;
    copy_bytes(reply->address, payload + 16u, 4u);
    copy_bytes(reply->server, ip + 12u, 4u);
    uint32_t option = DHCP_FIXED_SIZE;
    while (option < payload_length) {
        uint8_t code = payload[option++];
        if (code == 255u) {
            break;
        }
        if (code == 0u) {
            continue;
        }
        if (option >= payload_length) {
            return 0;
        }
        uint8_t length = payload[option++];
        if (option + length > payload_length) {
            return 0;
        }
        if (code == 53u && length == 1u) {
            reply->message_type = payload[option];
        } else if (code == 1u && length == 4u) {
            copy_bytes(reply->subnet_mask, payload + option, 4u);
        } else if (code == 3u && length >= 4u) {
            copy_bytes(reply->gateway, payload + option, 4u);
        } else if (code == 6u && length >= 4u) {
            copy_bytes(reply->dns_server, payload + option, 4u);
        } else if (code == 54u && length == 4u) {
            copy_bytes(reply->server, payload + option, 4u);
        }
        option += length;
    }
    return reply->message_type != 0u;
}

static int wait_for_dhcp(uint8_t message_type, struct dhcp_reply *reply)
{
    uint32_t start = pit_ticks_read();
    while ((uint32_t)(pit_ticks_read() - start) < DHCP_TIMEOUT_TICKS) {
        uint16_t frame_length;
        if (rtl8139_receive(receive_packet, sizeof(receive_packet),
                            &frame_length) &&
            parse_dhcp_reply(receive_packet, frame_length, reply)) {
            if (reply->message_type == message_type) {
                return 1;
            }
            if (reply->message_type == 6u) {
                return 0;
            }
        }
        __asm__ volatile ("hlt");
    }
    return 0;
}

static int send_arp_request(const uint8_t target[4])
{
    static const uint8_t broadcast[6] = {
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
    };
    write_ethernet_header(transmit_packet, broadcast, ETHERNET_ARP);
    uint8_t *arp = transmit_packet + ETHERNET_HEADER_SIZE;
    write_be16(arp, 1u);
    write_be16(arp + 2u, ETHERNET_IPV4);
    arp[4] = 6u;
    arp[5] = 4u;
    write_be16(arp + 6u, 1u);
    copy_bytes(arp + 8u, hardware_address, 6u);
    copy_bytes(arp + 14u, configuration.address, 4u);
    for (uint32_t index = 18u; index < 24u; ++index) {
        arp[index] = 0u;
    }
    copy_bytes(arp + 24u, target, 4u);
    for (uint32_t index = 42u; index < 60u; ++index) {
        transmit_packet[index] = 0u;
    }
    return rtl8139_send(transmit_packet, 60u);
}

static int wait_for_arp(const uint8_t target[4], uint8_t mac[6])
{
    uint32_t start = pit_ticks_read();
    while ((uint32_t)(pit_ticks_read() - start) < PING_TIMEOUT_TICKS) {
        uint16_t frame_length;
        if (rtl8139_receive(receive_packet, sizeof(receive_packet),
                            &frame_length) &&
            frame_length >= ETHERNET_HEADER_SIZE + 28u &&
            read_be16(receive_packet + 12u) == ETHERNET_ARP) {
            const uint8_t *arp = receive_packet + ETHERNET_HEADER_SIZE;
            if (read_be16(arp) == 1u &&
                read_be16(arp + 2u) == ETHERNET_IPV4 &&
                read_be16(arp + 6u) == 2u &&
                bytes_equal(arp + 14u, target, 4u) &&
                bytes_equal(arp + 24u, configuration.address, 4u)) {
                copy_bytes(mac, arp + 8u, 6u);
                return 1;
            }
        }
        __asm__ volatile ("hlt");
    }
    return 0;
}

static int send_icmp_echo(const uint8_t destination_mac[6],
                          const uint8_t destination_ip[4],
                          uint16_t sequence)
{
    const uint16_t icmp_length = 16u;
    write_ethernet_header(transmit_packet, destination_mac, ETHERNET_IPV4);
    uint8_t *ip = write_ipv4_header(transmit_packet, destination_ip,
                                    IPV4_ICMP, icmp_length);
    uint8_t *icmp = ip + IPV4_HEADER_SIZE;
    icmp[0] = 8u;
    icmp[1] = 0u;
    icmp[2] = 0u;
    icmp[3] = 0u;
    write_be16(icmp + 4u, ICMP_IDENTIFIER);
    write_be16(icmp + 6u, sequence);
    write_be32(icmp + 8u, DHCP_TRANSACTION_ID);
    write_be32(icmp + 12u, pit_ticks_read());
    write_be16(icmp + 2u, internet_checksum(icmp, icmp_length));
    return rtl8139_send(transmit_packet,
                        ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE +
                        icmp_length);
}

static int is_icmp_echo_reply(const uint8_t *frame, uint16_t frame_length,
                              const uint8_t source_ip[4], uint16_t sequence)
{
    if (frame_length < ETHERNET_HEADER_SIZE + IPV4_HEADER_SIZE + 8u ||
        read_be16(frame + 12u) != ETHERNET_IPV4) {
        return 0;
    }
    const uint8_t *ip = frame + ETHERNET_HEADER_SIZE;
    uint32_t header_length = (uint32_t)(ip[0] & 0x0fu) * 4u;
    uint16_t total_length = read_be16(ip + 2u);
    if ((ip[0] >> 4) != 4u || header_length < IPV4_HEADER_SIZE ||
        total_length < header_length + 8u ||
        total_length > frame_length - ETHERNET_HEADER_SIZE ||
        ip[9] != IPV4_ICMP ||
        !bytes_equal(ip + 12u, source_ip, 4u) ||
        !bytes_equal(ip + 16u, configuration.address, 4u)) {
        return 0;
    }
    const uint8_t *icmp = ip + header_length;
    return icmp[0] == 0u && icmp[1] == 0u &&
           read_be16(icmp + 4u) == ICMP_IDENTIFIER &&
           read_be16(icmp + 6u) == sequence;
}

int network_initialize(void)
{
    network_available = (uint8_t)rtl8139_initialize();
    configuration.configured = 0u;
    ipv4_identifier = 0u;
    if (network_available != 0u) {
        rtl8139_get_mac(hardware_address);
    }
    return network_available != 0u;
}

int network_is_available(void)
{
    return network_available != 0u;
}

int network_dhcp(void)
{
    if (network_available == 0u) {
        dhcp_error = "no RTL8139 interface";
        return 0;
    }
    static const uint8_t broadcast_ip[4] = {
        255u, 255u, 255u, 255u,
    };
    static const uint8_t broadcast_mac[6] = {
        0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
    };
    struct dhcp_reply offer = {0};
    struct dhcp_reply acknowledgment = {0};

    configuration.configured = 0u;
    for (uint32_t index = 0u; index < 4u; ++index) {
        configuration.address[index] = 0u;
        configuration.subnet_mask[index] = 0u;
        configuration.gateway[index] = 0u;
        configuration.dns_server[index] = 0u;
    }
    uint16_t payload_length = make_dhcp_message(dhcp_payload, 1u, 0);
    if (!send_udp(broadcast_mac, broadcast_ip, 68u, 67u,
                  dhcp_payload, payload_length)) {
        dhcp_error = "DHCP discovery transmit failed";
        return 0;
    }
    if (!wait_for_dhcp(2u, &offer) || offer.address[0] == 0u) {
        dhcp_error = "no valid DHCP offer received";
        return 0;
    }

    payload_length = make_dhcp_message(dhcp_payload, 3u, &offer);
    if (!send_udp(broadcast_mac, broadcast_ip, 68u, 67u,
                  dhcp_payload, payload_length)) {
        dhcp_error = "DHCP request transmit failed";
        return 0;
    }
    if (!wait_for_dhcp(5u, &acknowledgment) ||
        acknowledgment.address[0] == 0u) {
        dhcp_error = "no valid DHCP acknowledgment received";
        return 0;
    }
    copy_bytes(configuration.address, acknowledgment.address, 4u);
    copy_bytes(configuration.subnet_mask, acknowledgment.subnet_mask, 4u);
    copy_bytes(configuration.gateway, acknowledgment.gateway, 4u);
    copy_bytes(configuration.dns_server, acknowledgment.dns_server, 4u);
    configuration.configured = 1u;
    dhcp_error = 0;
    return 1;
}

const char *network_dhcp_error(void)
{
    return dhcp_error == 0 ? "unspecified DHCP error" : dhcp_error;
}

int network_ping(const uint8_t address[4], uint32_t *round_trip_ms)
{
    if (network_available == 0u || configuration.configured == 0u ||
        address == 0) {
        return 0;
    }

    uint8_t destination_mac[6];
    uint8_t next_hop[4];
    int same_subnet = 1;
    for (uint32_t index = 0u; index < 4u; ++index) {
        if ((address[index] & configuration.subnet_mask[index]) !=
            (configuration.address[index] & configuration.subnet_mask[index])) {
            same_subnet = 0;
        }
    }
    copy_bytes(next_hop, same_subnet ? address : configuration.gateway, 4u);
    if (!send_arp_request(next_hop) ||
        !wait_for_arp(next_hop, destination_mac) ||
        !send_icmp_echo(destination_mac, address, 1u)) {
        return 0;
    }

    uint32_t start = pit_ticks_read();
    while ((uint32_t)(pit_ticks_read() - start) < PING_TIMEOUT_TICKS) {
        uint16_t frame_length;
        if (rtl8139_receive(receive_packet, sizeof(receive_packet),
                            &frame_length) &&
            is_icmp_echo_reply(receive_packet, frame_length, address, 1u)) {
            if (round_trip_ms != 0) {
                *round_trip_ms =
                    (uint32_t)(pit_ticks_read() - start) * 10u;
            }
            return 1;
        }
        __asm__ volatile ("hlt");
    }
    return 0;
}

void network_get_config(struct network_config *config)
{
    if (config == 0) {
        return;
    }
    *config = configuration;
}

void network_get_mac(uint8_t address[6])
{
    if (address != 0) {
        copy_bytes(address, hardware_address, 6u);
    }
}
