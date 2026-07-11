//
// Created by janwin443 on 6/30/26.


#pragma once
#include <cstdint>

#include "core/universe/universe.h"

#pragma pack(push, 1)
struct ArtNetPackage {
    char id[8];
    uint16_t opCode;
    uint16_t protVer;
    uint8_t sequence;
    uint8_t physical;
    uint16_t subUniNet;
    uint16_t length;
    uint8_t data[512];
};
#pragma pack(pop)

namespace luminary::core {
    class ArtNetPlugin {
    public:
        static void send(const Universe& universe, const std::string& target_ip, uint16_t port);
    private:
        static ArtNetPackage build(const Universe &universe);
    };
}
