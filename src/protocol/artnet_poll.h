#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ArtPoll (OpCode 0x2000) und ArtPollReply (OpCode 0x2100), siehe Art-Net 4.
//
// Wie in artnet.h bewusst OHNE gepackte Structs: alle Felder werden Byte fuer
// Byte gelesen/geschrieben. Art-Net mischt little-endian (OpCode, Port) und
// big-endian (ProtVer, VersInfo, NumPorts) im selben Paket; mit expliziten
// Offsets ist die Reihenfolge sichtbar und unabhaengig von der Plattform.
// Dieses Modul kennt keine Sockets und ist damit isoliert testbar.
namespace luminary::core {

    constexpr uint16_t ARTNET_PORT = 0x1936;         // 6454
    constexpr uint16_t OP_POLL = 0x2000;
    constexpr uint16_t OP_POLL_REPLY = 0x2100;
    constexpr size_t ART_POLL_SIZE = 14;
    constexpr size_t ART_POLL_REPLY_MIN_SIZE = 207;  // bis einschliesslich Mac (aeltere Nodes)
    constexpr size_t ART_POLL_REPLY_SIZE = 239;      // Art-Net 4

    struct ArtNetNodePort {
        bool output = false;        // Node gibt DMX aus (Art-Net -> DMX)
        bool input = false;         // Node liest DMX ein (DMX -> Art-Net)
        uint16_t universe = 0;      // 15-bit Port-Address (Net/SubNet/Universe)
        bool data_active = false;   // GoodOutputA/GoodInput Bit 7: es fliessen Daten
    };

    struct ArtNetNode {
        std::string ip;             // Absender-IP des Pakets (die erreichbare Adresse)
        uint16_t port = 6454;
        std::string short_name;
        std::string long_name;
        std::string node_report;
        std::string mac;            // "aa:bb:cc:dd:ee:ff"
        uint16_t firmware = 0;      // VersInfo
        uint16_t oem = 0;
        uint16_t esta = 0;
        uint8_t style = 0;          // 0 = StNode, 1 = StController, ...
        uint8_t status1 = 0;
        uint8_t status2 = 0;
        uint8_t bind_index = 0;     // 0/1 = Root, >1 = weiteres Geraet hinter derselben IP
        uint8_t net = 0;
        uint8_t sub_net = 0;
        std::vector<ArtNetNodePort> ports;
        int64_t last_seen_ms = 0;   // vom Aufrufer gesetzt (steady clock)
    };

    // Baut ein ArtPoll-Paket (14 Byte). talk_to_me: Bit 1 = Reply bei Aenderungen.
    std::vector<uint8_t> build_art_poll(uint8_t talk_to_me = 0x00, uint8_t diag_priority = 0x10);

    // Prueft/parst ein ArtPollReply. Liefert nullopt bei fremden/kaputten Paketen
    // (falsche ID, falscher OpCode, zu kurz). Schneidet nie ueber `size` hinaus.
    std::optional<ArtNetNode> parse_art_poll_reply(const uint8_t* data, size_t size, const std::string& source_ip);

} // namespace luminary::core
