//
// Created by janwin443 on 6/27/26.
//

#include <iomanip>
#include <random>
#include <sstream>
#include "patch.h"

using namespace luminary::core;

std::string Patch::generate_id() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << dist(gen) << "-"
       << std::setw(4) << (dist(gen) & 0xFFFF) << "-"
       << std::setw(4) << ((dist(gen) & 0x0FFF) | 0x4000) << "-"
       << std::setw(4) << ((dist(gen) & 0x3FFF) | 0x8000) << "-"
       << std::setw(8) << dist(gen)
       << std::setw(4) << (dist(gen) & 0xFFFF);
    return ss.str();
}

bool Patch::has_collision(int universe_id, int start_address, int channel_count,
                          const std::string& exclude_id) const {
    for (const auto& entry : entries_) {
        // anderes Universe -> kein Problem
        if (entry.universe_id != universe_id) continue;

        // eigener Entry beim Move-Fall -> überspringen
        if (entry.id == exclude_id) continue;

        // wie viele Kanäle belegt der existierende Entry (in seinem
        // tatsächlich gepatchten Mode, nicht immer modes[0])?
        const int existing_channels = entry.fixture->modes.empty()
            ? 1
            : entry.fixture->modes[static_cast<size_t>(entry.mode_index)].channel_count;

        // Bereich des existierenden Entries
        const int existing_start = entry.start_address.value;
        const int existing_end   = existing_start + existing_channels - 1;

        // Bereich des neuen Entries
        const int new_start = start_address;
        const int new_end   = start_address + channel_count - 1;

        // überlappen sie sich?
        if (new_start <= existing_end && existing_start <= new_end) {
            return true;
        }
    }
    return false;
}

std::string Patch::add(std::shared_ptr<ofl::Fixture> fixture,
                        int universe_id,
                        DmxAddress start_address,
                        int mode_index) {
    if (mode_index < 0 || (!fixture->modes.empty() && mode_index >= static_cast<int>(fixture->modes.size()))) {
        throw std::out_of_range(
            "Invalid mode index " + std::to_string(mode_index) +
            " for fixture '" + fixture->name + "' (has " +
            std::to_string(fixture->modes.size()) + " modes)"
        );
    }

    // Wie viele Kanäle braucht die Fixture im gewählten Mode?
    const int channel_count = fixture->modes.empty()
        ? 1
        : fixture->modes[static_cast<size_t>(mode_index)].channel_count;

    // Adressbereich-Check: darf nicht über Kanal 512 hinausragen.
    // DmxAddress garantiert bereits start_address.value in [1, 512],
    // aber start_address + channel_count - 1 kann trotzdem drüber liegen.
    const int end_address = start_address.value + channel_count - 1;
    if (end_address > 512) {
        throw std::out_of_range(
            "Patch out of range in universe " + std::to_string(universe_id) +
            ": fixture '" + fixture->name + "' at address " +
            std::to_string(start_address.value) + " needs " +
            std::to_string(channel_count) + " channels, would end at " +
            std::to_string(end_address) + " (max 512)"
        );
    }

    // Kollision prüfen
    if (has_collision(universe_id, start_address.value, channel_count)) {
        throw std::runtime_error(
            "Patch collision in universe " + std::to_string(universe_id) +
            " at address " + std::to_string(start_address.value) +
            " for fixture '" + fixture->name + "'"
        );
    }

    // Entry bauen und einfügen
    PatchEntry entry {
        generate_id(),
        fixture,
        universe_id,
        start_address,
        mode_index
    };

    const std::string id = entry.id;
    entries_.push_back(std::move(entry));
    return id;
}

void Patch::remove(const std::string& entry_id) {
    std::erase_if(entries_, [&entry_id](const PatchEntry& entry) {
        return entry.id == entry_id;
    });
}

std::vector<const PatchEntry*> Patch::get_by_universe(int universe_id) const {
    std::vector<const PatchEntry*> result;
    for (const auto& entry : entries_) {
        if (entry.universe_id == universe_id) {
            result.push_back(&entry);
        }
    }
    return result;
}

const std::vector<PatchEntry> &Patch::entries() const {
    return entries_;
}

void Patch::clear_all() {
    entries_.clear();
}
