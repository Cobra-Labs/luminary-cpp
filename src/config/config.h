//
// Created by janwin443 on 6/19/26.
//

#ifndef LUMINARY_CONFIG_H
#define LUMINARY_CONFIG_H

#endif //LUMINARY_CONFIG_H

#pragma once
#include <string>
#include <vector>
#include <filesystem>

struct EngineConfig {
    std::string network_interface = "0.0.0.0";
    std::string artnet_target = "255.255.255.255";
    int artnet_port = 6454;
    int mtu = 1500;
    int universe_count = 4;
    int refresh_rate_hz = 40;
};

struct FixturesConfig {
    std::vector<std::string> search_paths = {};
};

struct EditorConfig {
    std::string units = "metric";
    double snap_grid_size = 0.1;
};

struct VisualizerConfig {
    std::string theme = "dark";
    std::string render_quality = "high";
};

struct Config {
    EngineConfig engine;
    FixturesConfig fixtures;
    EditorConfig editor;
    VisualizerConfig visualizer;
};

std::filesystem::path get_config_path();
Config load_config();