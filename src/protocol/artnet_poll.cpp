#include "artnet_poll.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

using namespace luminary::core;

namespace {
    // Liest ein NUL-terminiertes Textfeld fester Laenge. Nicht druckbare
    // Zeichen werden verworfen, damit fremde Nodes keine Steuerzeichen in
    // Logs, JSON oder UI einschleusen koennen.
    std::string read_text(const uint8_t* p, size_t max_len) {
        std::string out;
        for (size_t i = 0; i < max_len && p[i] != 0; ++i) {
            const uint8_t c = p[i];
            out.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?');
        }
        return out;
    }
}

std::vector<uint8_t> luminary::core::build_art_poll(uint8_t talk_to_me, uint8_t diag_priority) {
    std::vector<uint8_t> p(ART_POLL_SIZE, 0);
    std::memcpy(p.data(), "Art-Net", 8);            // "Art-Net" + NUL
    p[8] = OP_POLL & 0xFF;                          // OpCode low-byte-first
    p[9] = (OP_POLL >> 8) & 0xFF;
    p[10] = 0x00;                                   // ProtVer high-byte-first (14)
    p[11] = 14;
    p[12] = talk_to_me;
    p[13] = diag_priority;
    return p;
}

std::optional<ArtNetNode> luminary::core::parse_art_poll_reply(const uint8_t* d, size_t size, const std::string& source_ip) {
    if (!d || size < ART_POLL_REPLY_MIN_SIZE) return std::nullopt;
    if (std::memcmp(d, "Art-Net", 8) != 0) return std::nullopt;
    const uint16_t opcode = static_cast<uint16_t>(d[8] | (d[9] << 8));   // little-endian
    if (opcode != OP_POLL_REPLY) return std::nullopt;

    ArtNetNode n;
    n.ip = source_ip;
    n.port = static_cast<uint16_t>(d[14] | (d[15] << 8));                // little-endian
    n.firmware = static_cast<uint16_t>((d[16] << 8) | d[17]);            // big-endian
    n.net = d[18] & 0x7F;
    n.sub_net = d[19] & 0x0F;
    n.oem = static_cast<uint16_t>((d[20] << 8) | d[21]);
    n.status1 = d[23];
    n.esta = static_cast<uint16_t>(d[24] | (d[25] << 8));                // EstaManLo, EstaManHi
    n.short_name = read_text(d + 26, 18);
    n.long_name = read_text(d + 44, 64);
    n.node_report = read_text(d + 108, 64);
    n.style = d[200];

    char mac[18];
    std::snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", d[201], d[202], d[203], d[204], d[205], d[206]);
    n.mac = mac;

    if (size >= 212) n.bind_index = d[211];
    if (size >= 213) n.status2 = d[212];

    const unsigned num_ports = std::min<unsigned>(static_cast<unsigned>((d[172] << 8) | d[173]), 4u);
    for (unsigned i = 0; i < num_ports; ++i) {
        const uint8_t type = d[174 + i];
        const uint16_t base = static_cast<uint16_t>((n.net << 8) | (n.sub_net << 4));
        if (type & 0x80) {   // Output
            ArtNetNodePort port;
            port.output = true;
            port.universe = static_cast<uint16_t>(base | (d[190 + i] & 0x0F));   // SwOut
            port.data_active = (d[182 + i] & 0x80) != 0;                          // GoodOutputA
            n.ports.push_back(port);
        }
        if (type & 0x40) {   // Input
            ArtNetNodePort port;
            port.input = true;
            port.universe = static_cast<uint16_t>(base | (d[186 + i] & 0x0F));   // SwIn
            port.data_active = (d[178 + i] & 0x80) != 0;                          // GoodInput
            n.ports.push_back(port);
        }
    }
    return n;
}
