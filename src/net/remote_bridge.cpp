#include "remote_bridge.h"

#include <algorithm>

using namespace luminary::net;

bool RemoteBridge::is_single_line_object(const std::string& s) {
    if (s.size() < 2 || s.size() > MAX_BLOB_BYTES) return false;
    if (s.front() != '{' || s.back() != '}') return false;
    return s.find('\n') == std::string::npos && s.find('\r') == std::string::npos;
}

bool RemoteBridge::set_show(std::string json) {
    if (!is_single_line_object(json)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (json != show_) {          // nur echte Aenderungen erhoehen die Revision
        show_ = std::move(json);
        ++show_rev_;
    }
    return true;
}

bool RemoteBridge::set_live(std::string json) {
    if (!is_single_line_object(json)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (json != live_) {
        live_ = std::move(json);
        ++live_rev_;
    }
    return true;
}

RemoteBridge::Snapshot RemoteBridge::snapshot(uint64_t known_show_rev, uint64_t known_live_rev) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Snapshot s;
    s.show_rev = show_rev_;
    s.live_rev = live_rev_;
    if (show_rev_ != known_show_rev) s.show = show_;
    if (live_rev_ != known_live_rev) s.live = live_;
    return s;
}

void RemoteBridge::push_event(std::string json, const std::string& coalesce_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!coalesce_key.empty()) {
        auto it = std::find_if(events_.begin(), events_.end(),
                               [&](const auto& e) { return e.first == coalesce_key; });
        // Altes Ereignis entfernen und neues hinten anhaengen, damit die
        // Reihenfolge zu anderen Aktionen (z.B. Cue danach) erhalten bleibt.
        if (it != events_.end()) events_.erase(it);
    }
    if (events_.size() >= MAX_EVENTS) events_.pop_front();   // aeltestes verwerfen
    events_.emplace_back(coalesce_key, std::move(json));
}

std::vector<std::string> RemoteBridge::drain_events() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.reserve(events_.size());
    for (auto& e : events_) out.push_back(std::move(e.second));
    events_.clear();
    return out;
}
