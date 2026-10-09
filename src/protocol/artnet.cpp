//
// Created by janwin443 on 6/30/26.
//

#include <cstring>
#include "artnet.h"

#include "udp.h"
#include "config/config.h"

using namespace luminary::core;

// artnet.cpp
ArtNetPackage ArtNetPlugin::build(const Universe& universe, uint8_t grand_master) {
    ArtNetPackage packet{};
    std::memcpy(packet.id, "Art-Net", 8); // "Art-Net" + '\0', genau 8 Byte

    // OpCode 0x5000 (ArtDMX), low-byte-first laut Spec
    packet.opCodeLo = 0x00;
    packet.opCodeHi = 0x50;

    // ProtVer 14, high-byte-first laut Spec
    packet.protVerHi = 0x00;
    packet.protVerLo = 14; 

    packet.sequence = 0;
    packet.physical = 0;

    // Universe-Nummer auf Net (7 bit) + SubUni (8 bit) aufteilen.
    // Fuer Universe-IDs 0-127 (unser Anwendungsfall, siehe universe_count
    // in der Config) ist net() immer 0 - erst ab Universe 128 waere das
    // relevant. Trotzdem korrekt berechnet statt hart auf 0 gesetzt.
    const int uni = universe.id();
    packet.subUni = static_cast<uint8_t>(uni & 0xFF);
    packet.net    = static_cast<uint8_t>((uni >> 8) & 0x7F);

    // Length 512, high-byte-first laut Spec
    packet.lengthHi = static_cast<uint8_t>((512 >> 8) & 0xFF);
    packet.lengthLo = static_cast<uint8_t>(512 & 0xFF);

    const auto raw = universe.raw();
    if (grand_master == 255) {
        std::memcpy(packet.data, raw.data(), 512);
    } else {
        for (size_t i = 0; i < raw.size(); ++i) {
            packet.data[i] = static_cast<uint8_t>((static_cast<unsigned int>(raw[i]) * grand_master + 127u) / 255u);
        }
    }
    return packet;
}

void ArtNetPlugin::send(const Universe& universe, const std::string& target_ip, uint16_t port, uint8_t grand_master) {
    const ArtNetPackage packet = build(universe, grand_master);

    const int result = UDPManager::send(
        reinterpret_cast<const uint8_t*>(&packet),
        sizeof(packet),
        target_ip.c_str(),
        port
    );

    if (result != 0) {
        std::cerr << "[ArtNetPlugin::send] Error sending packet for universe "
                   << universe.id() << "\n";
    }
}