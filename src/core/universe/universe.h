//
// Created by janwin443 on 6/27/26.
//

#pragma once
#include <array>
#include <cstdint>
#include <mutex>

namespace luminary::core {

constexpr int DMX_CHANNELS = 512;

class Universe {
public:
    explicit Universe(int id);

    // Kopieren ergibt für ein "lebendes", gelocktes Objekt wie dieses
    // wenig Sinn - bewusst verboten. Verschieben ist dagegen nötig,
    // damit std::map::emplace() beim Anlegen der Universes in Engine
    // funktioniert (std::mutex ist selbst weder kopier- noch
    // verschiebbar, deshalb müssen wir das hier explizit definieren
    // statt uns auf den impliziten Compiler-generierten Move zu verlassen).
    Universe(const Universe&) = delete;
    Universe& operator=(const Universe&) = delete;
    Universe(Universe&& other) noexcept;
    Universe& operator=(Universe&& other) noexcept;

    // Einzelnen Kanal setzen (1-512)
    void set(int channel, uint8_t value);

    // Atomically write a 16-bit MSB/LSB pair so the Art-Net refresh thread
    // cannot observe a half-updated value.
    void set16(int msb_channel, int lsb_channel, uint16_t value);

    // Einzelnen Kanal lesen
    uint8_t get(int channel) const;

    // Ganzen Buffer auf einmal setzen (z.B. aus Art-Net-Paket)
    void set_raw(const std::array<uint8_t, DMX_CHANNELS>& data);

    // Kopie des rohen Buffers (für Art-Net-Sender). Bewusst eine Kopie
    // statt einer Referenz: mehrere Threads (Socket-Clients, refresh_loop)
    // greifen gleichzeitig zu - eine Referenz würde entweder den Lock nach
    // aussen durchreichen (fehleranfällig) oder ganz ohne Schutz zugreifen.
    // 512 Byte kopieren ist günstig genug, um das nicht zu rechtfertigen.
    std::array<uint8_t, DMX_CHANNELS> raw() const;

    // Alles auf 0 (Blackout)
    void blackout();

    int id() const { return id_; }

private:
    int id_;
    std::array<uint8_t, DMX_CHANNELS> data_ = {};
    mutable std::mutex mutex_;
};

} // namespace luminary::core