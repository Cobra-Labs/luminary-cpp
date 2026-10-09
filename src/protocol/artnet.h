//
// Created by janwin443 on 6/30/26.


#pragma once
#include <cstdint>
#include <string>

#include "core/universe/universe.h"

// Wire-Format der Art-Net ArtDMX-Nachricht, siehe Art-Net 4 Spec:
// https://art-net.org.uk/downloads/art-net.pdf
//
// Bewusst NUR uint8_t-Felder statt uint16_t + htons()/ntohs():
// Art-Net mischt innerhalb eines Pakets big-endian Felder (ProtVer,
// Length) mit little-endian Feldern (OpCode) - sich auf htons() (das
// IMMER big-endian erzeugt) zu verlassen, ist fuer die Haelfte der
// Felder schlicht falsch. Mit reinen Byte-Feldern ist die Reihenfolge
// explizit sichtbar und unabhaengig von der Endianness der Zielplattform.
#pragma pack(push, 1)
struct ArtNetPackage {
    char id[8];           // "Art-Net" + Nullbyte

    uint8_t opCodeLo;      // OpCode wird low-byte-first uebertragen
    uint8_t opCodeHi;      // (0x5000 fuer ArtDMX -> Lo=0x00, Hi=0x50)

    uint8_t protVerHi;     // ProtVer wird high-byte-first uebertragen
    uint8_t protVerLo;     // (aktuell 14 -> Hi=0x00, Lo=0x0E)

    uint8_t sequence;
    uint8_t physical;

    uint8_t subUni;        // Port-Address low byte (Universe & 0xFF)
    uint8_t net;           // Port-Address high byte / Net (Universe >> 8, 7 bit)

    uint8_t lengthHi;      // Laenge der DMX-Daten, high-byte-first
    uint8_t lengthLo;

    uint8_t data[512];
};
#pragma pack(pop)

namespace luminary::core {
    class ArtNetPlugin {
    public:
        static void send(const Universe& universe, const std::string& target_ip, uint16_t port, uint8_t grand_master = 255);
    private:
        static ArtNetPackage build(const Universe &universe, uint8_t grand_master);
    };
}
