#pragma once
#include <atomic>
#include <iostream>
#include <string>

namespace luminary::util {

    // Wird in main.cpp per -l/--log Flag auf true gesetzt.
    inline std::atomic<bool> g_verbose_logging{false};

    inline void log(const std::string& message) {
        if (!g_verbose_logging.load(std::memory_order_relaxed)) return;
        std::cout << "[luminary] " << message << std::endl;
    }

} // namespace luminary::util
