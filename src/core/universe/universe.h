//
// Created by janwin443 on 6/27/26.
//

#pragma once
#include <array>
#include <cstdint>

namespace luminary::core {

constexpr int DMX_CHANNELS = 512;

class Universe {
public:
    explicit Universe(int id);

    // Einzelnen Kanal setzen (1-512)
    void set(int channel, uint8_t value);

    // Einzelnen Kanal lesen
    uint8_t get(int channel) const;

    // Ganzen Buffer auf einmal setzen (z.B. aus Art-Net-Paket)
    void set_raw(const std::array<uint8_t, DMX_CHANNELS>& data);

    // Zugriff auf rohen Buffer (für Art-Net-Sender)
    const std::array<uint8_t, DMX_CHANNELS>& raw() const;

    // Alles auf 0 (Blackout)
    void blackout();

    int id() const { return id_; }

private:
    int id_;
    std::array<uint8_t, DMX_CHANNELS> data_ = {};
};

} // namespace luminary::core