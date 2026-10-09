#include "node_discovery.h"
#include "util/log.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

using namespace luminary::net;
using namespace luminary::core;
using namespace luminary::util;

namespace {
    int64_t now_ms() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
}

NodeDiscovery::NodeDiscovery(std::function<std::string()> target_provider, uint16_t port, std::string broadcast_address)
    : target_provider_(std::move(target_provider)), port_(port), broadcast_address_(std::move(broadcast_address)) {}

NodeDiscovery::~NodeDiscovery() { stop(); }

void NodeDiscovery::set_error(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_error_ = message;
}

std::string NodeDiscovery::last_error() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

void NodeDiscovery::start() {
    if (running_) return;

    if (!sock_init()) {
        set_error("Winsock konnte nicht initialisiert werden");
        return;
    }
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ == INVALID_SOCK) {
        set_error("socket: " + sock_error());
        return;
    }
    sock_set_nonblocking(fd_);
    sock_disable_udp_connreset(fd_);

    const int on = 1;
    const char* on_ptr = reinterpret_cast<const char*>(&on);   // Winsock erwartet char*
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, on_ptr, sizeof(on));
#ifdef SO_REUSEPORT
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, on_ptr, sizeof(on));
#endif
    if (::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, on_ptr, sizeof(on)) < 0) {
        set_error("SO_BROADCAST: " + sock_error());
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        set_error("bind port " + std::to_string(port_) + ": " + sock_error());
        sock_close(fd_);
        fd_ = INVALID_SOCK;
        return;
    }

    set_error("");
    running_ = true;
    poll_requested_ = true;   // direkt beim Start einmal suchen
    thread_ = std::thread(&NodeDiscovery::run, this);
}

void NodeDiscovery::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (fd_ != INVALID_SOCK) { sock_close(fd_); fd_ = INVALID_SOCK; }
}

void NodeDiscovery::poll_now() { poll_requested_ = true; }

void NodeDiscovery::send_poll() {
    const auto packet = build_art_poll();

    std::vector<std::string> targets{broadcast_address_};
    if (target_provider_) {
        const std::string target = target_provider_();
        if (!target.empty() && target != broadcast_address_) targets.push_back(target);
    }

    for (const auto& ip : targets) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(port_);
        if (inet_pton(AF_INET, ip.c_str(), &dest.sin_addr) != 1) continue;
        if (::sendto(fd_, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                     reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) < 0) {
            log("NodeDiscovery: ArtPoll an " + ip + " fehlgeschlagen: " + sock_error());
        }
    }
}

void NodeDiscovery::handle_datagram(const uint8_t* data, size_t size, const std::string& source_ip) {
    auto parsed = parse_art_poll_reply(data, size, source_ip);
    if (!parsed) return;   // ArtDMX, eigene ArtPolls und Fremdpakete landen hier

    parsed->last_seen_ms = now_ms();

    std::lock_guard<std::mutex> lock(mutex_);
    // Ein Geraet mit mehreren Bind-Indizes antwortet pro Index einzeln.
    auto it = std::find_if(nodes_.begin(), nodes_.end(), [&](const ArtNetNode& n) {
        return n.mac == parsed->mac && n.bind_index == parsed->bind_index && n.ip == parsed->ip;
    });
    if (it == nodes_.end()) {
        log("NodeDiscovery: neuer Node " + parsed->short_name + " @ " + parsed->ip);
        nodes_.push_back(std::move(*parsed));
    } else {
        *it = std::move(*parsed);
    }
}

void NodeDiscovery::run() {
    int64_t next_poll = 0;
    std::vector<uint8_t> buffer(1024);

    while (running_) {
        const int64_t t = now_ms();
        if (poll_requested_.exchange(false) || t >= next_poll) {
            send_poll();
            next_poll = t + POLL_INTERVAL_MS;
        }

        if (sock_wait_readable(fd_, 100) > 0) {
            for (int burst = 0; burst < 64; ++burst) {   // aufgelaufene Pakete abholen
                sockaddr_in from{};
                sock_len_t from_len = sizeof(from);
                const int n = static_cast<int>(::recvfrom(fd_, reinterpret_cast<char*>(buffer.data()),
                                                          static_cast<int>(buffer.size()), SOCK_RECV_FLAGS,
                                                          reinterpret_cast<sockaddr*>(&from), &from_len));
                if (n <= 0) break;
                char ip[INET_ADDRSTRLEN] = {};
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                handle_datagram(buffer.data(), static_cast<size_t>(n), ip);
            }
        }

        // Abgelaufene Nodes entfernen
        std::lock_guard<std::mutex> lock(mutex_);
        const int64_t cutoff = now_ms() - NODE_TIMEOUT_MS;
        std::erase_if(nodes_, [&](const ArtNetNode& n) { return n.last_seen_ms < cutoff; });
    }
}

std::vector<ArtNetNode> NodeDiscovery::nodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto copy = nodes_;
    std::sort(copy.begin(), copy.end(), [](const ArtNetNode& a, const ArtNetNode& b) {
        return a.ip == b.ip ? a.bind_index < b.bind_index : a.ip < b.ip;
    });
    return copy;
}
