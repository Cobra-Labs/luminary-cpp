//
// Created by janwin443 on 6/27/26.
//

#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../dmx_types.h"
#include "ofl/types.h"

namespace luminary::core {
    struct PatchEntry {
        std::string id;
        std::shared_ptr<ofl::Fixture> fixture;
        int universe_id;
        DmxAddress start_address;
        int mode_index = 0; // Index in fixture->modes - welcher Mode gepatcht wurde
    };

    class Patch {
    public:
        // Fixture zum Patch hinzufügen – gibt ID des PatchEntry zurück.
        // mode_index waehlt, welcher Mode aus fixture->modes gepatcht wird
        // (0 = erster/Default-Mode). Wird NICHT auf modes.size() geprueft -
        // das macht add() selbst und wirft bei ungueltigem Index.
        std::string add(std::shared_ptr<ofl::Fixture> fixture, int universe_id,
                         DmxAddress start_address, int mode_index = 0);

        // PatchEntry entfernen
        void remove(const std::string &entry_id);

        // Alle Entries in einem Universe holen (für Art-Net-Sender)
        [[nodiscard]] std::vector<const PatchEntry *> get_by_universe(int universe_id) const;

        // Alle Entries
        [[nodiscard]] const std::vector<PatchEntry> &entries() const;

        // Blackout – alle Universes auf 0
        void clear_all();

    private:
        std::vector<PatchEntry> entries_;

        // Prüft, ob neue Adresse mit bestehenden Entries kollidiert
        // (start_address bis start_address + fixture.channel_count - 1)
        [[nodiscard]] bool has_collision(int universe_id, int start_address, int channel_count, const std::string &exclude_id = "") const;

        // UUID für neuen PatchEntry generieren
        static std::string generate_id();

    };
} // namespace luminary::core
