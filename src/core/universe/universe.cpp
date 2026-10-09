//
// Created by janwin443 on 6/27/26.
//

#include "universe.h"
#include <stdexcept>

namespace luminary::core {

    Universe::Universe(int id) : id_(id) {
        data_.fill(0);
    }

    Universe::Universe(Universe&& other) noexcept : id_(other.id_) {
        // Zum Konstruktionszeitpunkt (Engine::Engine() beim Start, vor
        // jeglichem Thread-Zugriff) waere Locking hier eigentlich
        // unnoetig - trotzdem sauber gemacht, falls Universe irgendwann
        // auch nach dem Start bewegt wird.
        std::lock_guard<std::mutex> lock(other.mutex_);
        data_ = other.data_;
    }

    Universe& Universe::operator=(Universe&& other) noexcept {
        if (this == &other) return *this;
        std::scoped_lock lock(mutex_, other.mutex_);
        id_ = other.id_;
        data_ = other.data_;
        return *this;
    }

    void Universe::set(int channel, uint8_t value) {
        if (channel < 1 || channel > DMX_CHANNELS) {
            throw std::out_of_range(
                "DMX channel " + std::to_string(channel) +
                " out of range (1-512) in universe " + std::to_string(id_)
            );
        }
        std::lock_guard<std::mutex> lock(mutex_);
        data_[channel - 1] = value;
    }

    void Universe::set16(int msb_channel, int lsb_channel, uint16_t value) {
        if (msb_channel < 1 || msb_channel > DMX_CHANNELS ||
            lsb_channel < 1 || lsb_channel > DMX_CHANNELS ||
            msb_channel == lsb_channel) {
            throw std::out_of_range("Invalid 16-bit DMX channel pair in universe " + std::to_string(id_));
        }
        std::lock_guard<std::mutex> lock(mutex_);
        data_[msb_channel - 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        data_[lsb_channel - 1] = static_cast<uint8_t>(value & 0xFF);
    }

    uint8_t Universe::get(int channel) const {
        if (channel < 1 || channel > DMX_CHANNELS) {
            throw std::out_of_range(
                "DMX channel " + std::to_string(channel) +
                " out of range (1-512) in universe " + std::to_string(id_)
            );
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return data_[channel - 1];
    }

    void Universe::set_raw(const std::array<uint8_t, DMX_CHANNELS>& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_ = data;
    }

    std::array<uint8_t, DMX_CHANNELS> Universe::raw() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_; // Kopie, absichtlich - siehe Kommentar im Header
    }

    void Universe::blackout() {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.fill(0);
    }

} // namespace luminary::core
