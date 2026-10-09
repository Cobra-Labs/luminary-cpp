//
// Created by janwin443 on 6/19/26.
//

#include "engine.h"

#include <chrono>
#include <ranges>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include "config/config.h"
#include "core/universe/universe.h"
#include "protocol/artnet.h"

using namespace luminary::core;

Engine::Engine(EngineConfig config) : config_(std::move(config)) {
    if (config_.universe_count < 1) config_.universe_count = 1;
    if (config_.universe_count > 128) config_.universe_count = 128;
    if (config_.refresh_rate_hz != 44) config_.refresh_rate_hz = 40;
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
    const bool was_running = running_.exchange(false);
    if (refresh_thread_.joinable()) {
        refresh_thread_.join();
    }
    if (!was_running) return;

    // Sauberes Beenden: Fail-Safe-Kanaele (Nebel) auf 0 und noch ein paar
    // Frames senden, damit die Node es sieht - sonst hielte sie den letzten Wert.
    apply_failsafe();
    for (int i = 0; i < 3; ++i) {
        send_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

bool Engine::universe_active(int id) const {
    if (id < 0 || id >= static_cast<int>(patched_.size())) return true;   // unbekannt: lieber senden
    return patched_[id].load(std::memory_order_relaxed) || touched_[id].load(std::memory_order_relaxed);
}

void Engine::refresh_patched_universes() {
    std::array<bool, 128> now{};
    for (const auto& entry : patch_.entries()) {
        if (entry.universe_id >= 0 && entry.universe_id < static_cast<int>(now.size())) now[entry.universe_id] = true;
    }
    for (size_t i = 0; i < now.size(); ++i) patched_[i].store(now[i], std::memory_order_relaxed);
}

std::vector<int> Engine::active_universes() const {
    std::vector<int> out;
    for (const auto& [id, u] : universes_) {
        (void)u;
        if (universe_active(id)) out.push_back(id);
    }
    return out;
}

void Engine::set_failsafe_channels(std::vector<std::pair<int, int>> channels) {
    std::lock_guard<std::mutex> lock(failsafe_mutex_);
    failsafe_channels_ = std::move(channels);
}

void Engine::apply_failsafe() {
    std::vector<std::pair<int, int>> channels;
    {
        std::lock_guard<std::mutex> lock(failsafe_mutex_);
        channels = failsafe_channels_;
    }
    for (const auto& [universe_id, channel] : channels) {
        auto it = universes_.find(universe_id);
        if (it == universes_.end() || channel < 1 || channel > DMX_CHANNELS) continue;
        it->second.set(channel, 0);
        if (universe_id >= 0 && universe_id < static_cast<int>(touched_.size())) touched_[universe_id].store(true);
    }
}

void Engine::send_frame() {
    if (!output_enabled_.load()) return;
    int artnet_port;
    std::string target;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        artnet_port = config_.artnet_port;
        target = config_.artnet_target;
    }
    const uint8_t master = grand_master_.load(std::memory_order_relaxed);
    for (auto& [id, u] : universes_) {
        if (!universe_active(id)) continue;
        ArtNetPlugin::send(u, target, static_cast<uint16_t>(artnet_port), master);
    }
}

void Engine::refresh_loop() {
    using namespace std::chrono;

    while (running_) {
        const auto tick_start = steady_clock::now();
        int refresh_hz;
        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            refresh_hz = config_.refresh_rate_hz;
        }

        send_frame();

        const auto interval = milliseconds(std::max(1, 1000 / std::max(1, refresh_hz)));
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
    if (universe_id >= 0 && universe_id < static_cast<int>(touched_.size())) touched_[universe_id].store(true, std::memory_order_relaxed);
}

void Engine::set_16(int universe_id, int msb_channel, int lsb_channel, uint16_t value) {
    universe(universe_id).set16(msb_channel, lsb_channel, value);
    if (universe_id >= 0 && universe_id < static_cast<int>(touched_.size())) touched_[universe_id].store(true, std::memory_order_relaxed);
}

void Engine::blackout() {
    for (auto &u: universes_ | std::views::values) u.blackout();
}

void Engine::set_artnet_target(const std::string& target) {
    if (target.empty() || target.size() > 45 || target.find_first_of("\r\n \t") != std::string::npos)
        throw std::invalid_argument("Invalid Art-Net target");
    {
        sockaddr_in addr{};
        if (inet_pton(AF_INET, target.c_str(), &addr.sin_addr) != 1)
            throw std::invalid_argument("Art-Net target must be a valid IPv4 address");
    }
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        config_.artnet_target = target;
    }
    output_enabled_.store(true);   // ab jetzt ist ein Ziel bewusst gewaehlt
}

void Engine::set_refresh_rate_hz(int hz) {
    if (hz != 40 && hz != 44)
        throw std::out_of_range("Art-Net refresh rate must be 40 or 44 Hz");
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_.refresh_rate_hz = hz;
}

void Engine::set_grand_master(int value) {
    if (value < 0 || value > 255) throw std::out_of_range("Grand master must be 0-255");
    grand_master_.store(static_cast<uint8_t>(value), std::memory_order_relaxed);
}

int Engine::grand_master() const {
    return static_cast<int>(grand_master_.load(std::memory_order_relaxed));
}

int Engine::refresh_rate_hz() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_.refresh_rate_hz;
}

std::string Engine::artnet_target() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return config_.artnet_target;
}

void Engine::set_fixture_catalog(std::vector<std::pair<std::string, std::shared_ptr<ofl::Fixture>>> catalog) { fixture_catalog_ = std::move(catalog); }
