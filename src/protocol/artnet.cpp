//
// Created by janwin443 on 6/30/26.
//

#include <cstring>
#include "artnet.h"

#include <netinet/in.h>

#include "udp.h"
#include "config/config.h"

using namespace luminary::core;

// artnet.cpp
ArtNetPackage ArtNetPlugin::build(const Universe& universe) {
    ArtNetPackage packet{};
    std::memcpy(packet.id, "Art-Net", 8);
    packet.opCode = 0x5000;
    packet.protVer = htons(14);
    packet.sequence = 0;
    packet.physical = 0;
    packet.subUniNet = htons(universe.id()); // Universe-ID korrekt setzen
    packet.length = htons(512);
    std::memcpy(packet.data, universe.raw().data(), 512); // echte Daten
    return packet;
}

void ArtNetPlugin::send(const Universe& universe, const std::string& target_ip, uint16_t port) {
    const ArtNetPackage packet = build(universe);

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