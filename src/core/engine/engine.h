#pragma once
#include <map>
#include <thread>
#include <atomic>
#include "core/universe/universe.h"
#include "core/patch/patch.h"
#include "../../config/config.h"

namespace luminary::core {

    class Engine {
    public:
        explicit Engine(EngineConfig config);

        void start();
        void stop();

        Patch& patch();
        Universe& universe(int id);
        void set_channel(int universe_id, int channel, uint8_t value);
        void blackout();

    private:
        std::map<int, Universe> universes_{};
        Patch patch_{};
        EngineConfig config_;

        std::thread refresh_thread_;
        std::atomic<bool> running_ = false;

        void refresh_loop();
    };

} // namespace luminary::core