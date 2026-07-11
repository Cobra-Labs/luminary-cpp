//
// Created by janwin443 on 6/27/26.
//

#include "universe.h"
#include <stdexcept>

namespace luminary::core {

    Universe::Universe(int id) : id_(id) {
        data_.fill(0);
    }

    void Universe::set(int channel, uint8_t value) {
        if (channel < 1 || channel > DMX_CHANNELS) {
            throw std::out_of_range(
                "DMX channel " + std::to_string(channel) +
                " out of range (1-512) in universe " + std::to_string(id_)
            );
        }
        data_[channel - 1] = value;
    }

    uint8_t Universe::get(int channel) const {
        if (channel < 1 || channel > DMX_CHANNELS) {
            throw std::out_of_range(
                "DMX channel " + std::to_string(channel) +
                " out of range (1-512) in universe " + std::to_string(id_)
            );
        }
        return data_[channel - 1];
    }

    void Universe::set_raw(const std::array<uint8_t, DMX_CHANNELS>& data) {
        data_ = data;
    }

    const std::array<uint8_t, DMX_CHANNELS>& Universe::raw() const {
        return data_;
    }

    void Universe::blackout() {
        data_.fill(0);
    }

} // namespace luminary::core
