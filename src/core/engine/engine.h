#pragma once
#include <array>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include "core/universe/universe.h"
#include "core/patch/patch.h"
#include "../../config/config.h"
#include "ofl/types.h"
#include <memory>
#include <vector>

namespace luminary::core {

    class Engine {
    public:
        explicit Engine(EngineConfig config);

        void start();
        void stop();

        Patch& patch();
        Universe& universe(int id);
        void set_channel(int universe_id, int channel, uint8_t value);
        void set_16(int universe_id, int msb_channel, int lsb_channel, uint16_t value);
        void blackout();
        void set_artnet_target(const std::string& target);
        void set_refresh_rate_hz(int hz);
        void set_grand_master(int value);
        int grand_master() const;
        // --- Ausgabe-Steuerung --------------------------------------------------
        // Ohne gesetztes Ziel nichts senden: sonst gehen beim Start Pakete an das
        // Standardziel (Broadcast), bevor die Desktop-App ihr gespeichertes Ziel
        // gesetzt hat. set_artnet_target() schaltet die Ausgabe ein.
        void set_output_enabled(bool enabled) { output_enabled_.store(enabled); }
        bool output_enabled() const { return output_enabled_.load(); }

        // Nur Universes senden, die gepatcht sind oder in die geschrieben wurde,
        // statt immer alle universe_count Stueck (z.B. 4 x 40 Hz fuer nichts).
        void refresh_patched_universes();          // nach PATCH/UNPATCH/CLEAR aufrufen
        std::vector<int> active_universes() const;

        // Fail-Safe: Kanaele (z.B. Nebel), die bei Absturz der Desktop-App und
        // beim Beenden der Engine auf 0 gesetzt werden.
        void set_failsafe_channels(std::vector<std::pair<int, int>> channels);
        void apply_failsafe();

        int universe_count() const { return static_cast<int>(universes_.size()); }
        int refresh_rate_hz() const;
        std::string artnet_target() const;
        void set_fixture_catalog(std::vector<std::pair<std::string, std::shared_ptr<ofl::Fixture>>> catalog);
        const auto& fixture_catalog() const { return fixture_catalog_; }

    private:
        std::map<int, Universe> universes_{};
        Patch patch_{};
        EngineConfig config_;

        std::thread refresh_thread_;
        std::atomic<bool> running_ = false;
        mutable std::mutex config_mutex_;
        std::atomic<uint8_t> grand_master_{255};
        std::vector<std::pair<std::string, std::shared_ptr<ofl::Fixture>>> fixture_catalog_;

        void refresh_loop();
        void send_frame();
        bool universe_active(int id) const;

        std::atomic<bool> output_enabled_{true};
        std::array<std::atomic<bool>, 128> patched_{};
        std::array<std::atomic<bool>, 128> touched_{};
        mutable std::mutex failsafe_mutex_;
        std::vector<std::pair<int, int>> failsafe_channels_;
    };

} // namespace luminary::core