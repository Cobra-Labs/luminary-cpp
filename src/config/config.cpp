//
// Created by janwin443 on 6/19/26.
//

#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <sstream>
#include "toml++/toml.hpp"
#include "config.h"

std::filesystem::path get_config_path() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (!appdata) {
        // Fallback, falls APPDATA nicht existiert
        const char* userprofile = std::getenv("USERPROFILE");
        return std::filesystem::path(userprofile ? userprofile : ".") / "AppData" / "Roaming" / "luminary" / "config.toml";
    }
    return std::filesystem::path(appdata) / "luminary" / "config.toml";
#else
    if (const char* xdg_config = std::getenv("XDG_CONFIG_HOME"); xdg_config && *xdg_config != '\0') {
        return std::filesystem::path(xdg_config) / "luminary" / "config.toml";
    }

    // Fallback auf ~/.config
    const char* home = std::getenv("HOME");
    if (!home) {
        return std::filesystem::current_path() / "luminary" / "config.toml"; // Letzter Ausweg
    }
    return std::filesystem::path(home) / ".config" / "luminary" / "config.toml";
#endif
}

Config load_config() {
    Config cfg;

    const auto path = get_config_path();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto fixture_dir = path.parent_path() / "fixtures";
    std::filesystem::create_directories(fixture_dir, ec);
    if (!std::filesystem::exists(path)) {
        // Keep the engine's on-disk defaults deterministic. In particular,
        // Art-Net runs at 40 Hz by default; 44 Hz is supported as an
        // explicit compatibility option.
        std::ofstream out(path);
        if (out) {
            out << "[engine]\n"
                << "network_interface = \"0.0.0.0\"\n"
                << "artnet_target = \"255.255.255.255\"\n"
                << "artnet_port = 6454\n"
                << "mtu = 1500\n"
                << "universe_count = 4\n"
                << "refresh_rate_hz = 40\n\n"
                << "[fixtures]\n"
                << "search_paths = [\"" << fixture_dir.string() << "\"]\n";
        }
        cfg.fixtures.search_paths.push_back(fixture_dir.string());
        return cfg;
    }

    try {
        toml::table tbl = toml::parse_file(path.string());

        if (auto* engine = tbl["engine"].as_table()) {
            cfg.engine.network_interface = (*engine)["network_interface"].value_or(cfg.engine.network_interface);
            cfg.engine.artnet_target = (*engine)["artnet_target"].value_or(cfg.engine.artnet_target);
            cfg.engine.artnet_port = (*engine)["artnet_port"].value_or(cfg.engine.artnet_port);
            cfg.engine.mtu = (*engine)["mtu"].value_or(cfg.engine.mtu);
            cfg.engine.universe_count = (*engine)["universe_count"].value_or(cfg.engine.universe_count);
            cfg.engine.refresh_rate_hz = (*engine)["refresh_rate_hz"].value_or(cfg.engine.refresh_rate_hz);
        }

        if (auto* editor = tbl["editor"].as_table()) {
            cfg.editor.units = (*editor)["units"].value_or(cfg.editor.units);
            cfg.editor.snap_grid_size = (*editor)["snap_grid_size"].value_or(cfg.editor.snap_grid_size);
        }

        if (auto* visualizer = tbl["visualizer"].as_table()) {
            cfg.visualizer.theme = (*visualizer)["theme"].value_or(cfg.visualizer.theme);
            cfg.visualizer.render_quality = (*visualizer)["render_quality"].value_or(cfg.visualizer.render_quality);
        }

        if (auto* fixtures = tbl["fixtures"].as_table()) {
            if (auto* search_paths = (*fixtures)["search_paths"].as_array()) {
                for (auto&& elem : *search_paths) {
                    if (auto str = elem.template value<std::string>()) {
                        cfg.fixtures.search_paths.push_back(*str);
                    }
                }
            }
        }

    } catch (const toml::parse_error& err) {
        std::cerr << "Config-Fehler: " << err.description() << " (" << path << ")\n";
        return Config{};
    }

    if (cfg.fixtures.search_paths.empty()) {
        cfg.fixtures.search_paths.push_back((path.parent_path() / "fixtures").string());
    }
    // Art-Net is intentionally restricted to the stable supported rates.
    if (cfg.engine.refresh_rate_hz != 44) cfg.engine.refresh_rate_hz = 40;
    return cfg;
}