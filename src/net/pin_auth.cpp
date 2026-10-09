#include "pin_auth.h"
#include <iomanip>
#include <sstream>

using namespace luminary::net;

PinAuth::PinAuth() : rng_(std::random_device{}()) {}

std::string PinAuth::generate_pin(int digits, std::chrono::seconds validity) {
    std::lock_guard<std::mutex> lock(mutex_);

    const int max_value = [&] {
        int v = 1;
        for (int i = 0; i < digits; i++) v *= 10;
        return v;
    }();

    std::uniform_int_distribution<int> dist(0, max_value - 1);
    const int value = dist(rng_);

    std::ostringstream oss;
    oss << std::setw(digits) << std::setfill('0') << value;
    current_pin_ = oss.str();
    pin_expiry_ = std::chrono::steady_clock::now() + validity;
    pin_consumed_ = false;

    return current_pin_;
}

std::string PinAuth::redeem_pin(const std::string& pin) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (pin_consumed_) return "";
    if (pin != current_pin_) return "";
    if (std::chrono::steady_clock::now() > pin_expiry_) return "";

    // Single-Use: PIN ist nach dem ersten erfolgreichen Einloesen sofort tot,
    // auch wenn die Gueltigkeitsdauer technisch noch nicht abgelaufen waere.
    pin_consumed_ = true;

    const std::string token = generate_random_hex_locked(32);
    valid_tokens_.insert(token);
    return token;
}

bool PinAuth::is_valid_token(const std::string& token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return valid_tokens_.count(token) > 0;
}

void PinAuth::revoke_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    valid_tokens_.erase(token);
}

void PinAuth::revoke_all_tokens() {
    std::lock_guard<std::mutex> lock(mutex_);
    valid_tokens_.clear();
}

std::string PinAuth::generate_random_hex_locked(size_t bytes) {
    // Erwartet: mutex_ ist von der aufrufenden Funktion bereits gehalten.
    static const char hex_chars[] = "0123456789abcdef";
    std::uniform_int_distribution<int> byte_dist(0, 255);

    std::string result;
    result.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; i++) {
        const int byte = byte_dist(rng_);
        result += hex_chars[(byte >> 4) & 0xF];
        result += hex_chars[byte & 0xF];
    }
    return result;
}
