//
// Created by janwin443 on 6/19/26.
//

#include "engine.h"

#include <ranges>

#include "config/config.h"
#include "core/universe/universe.h"
#include "protocol/artnet.h"

using namespace luminary::core;

Engine::Engine(EngineConfig config) : config_(std::move(config)) {
    // Universes anlegen basierend auf universe_count aus der Config
    for (int i = 0; i < config_.universe_count; i++) {
        universes_.emplace(i, Universe(i));
    }
}

void Engine::start() {
    if (running_) return;
    running_ = true;
    refresh_thread_ = std::thread(&Engine::refresh_loop, this);
}

void Engine::stop() {
    running_ = false;
    if (refresh_thread_.joinable()) {
        refresh_thread_.join();
    }
}

void Engine::refresh_loop() {
    using namespace std::chrono;
    const auto interval = milliseconds(1000 / config_.refresh_rate_hz);

    while (running_) {
        auto tick_start = steady_clock::now();

        for (auto& [id, u] : universes_) {
            ArtNetPlugin::send(u, config_.artnet_target, config_.artnet_port);
        }

        // auf nächsten Tick warten
        std::this_thread::sleep_until(tick_start + interval);
    }
}

Patch& Engine::patch() { return patch_; }

Universe& Engine::universe(int id) {
    auto it = universes_.find(id);
    if (it == universes_.end())
        throw std::out_of_range("Universe " + std::to_string(id) + " not found");
    return it->second;
}

void Engine::set_channel(int universe_id, int channel, uint8_t value) {
    universe(universe_id).set(channel, value);
}

void Engine::blackout() {
    for (auto &u: universes_ | std::views::values) u.blackout();
}
