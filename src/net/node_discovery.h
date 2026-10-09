#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "protocol/artnet_poll.h"
#include "util/sock_compat.h"

namespace luminary::net {

    // Art-Net Node-Discovery: sendet periodisch ArtPoll und sammelt die
    // ArtPollReply-Antworten der Nodes im Netz.
    //
    // Ein einziger UDP-Socket auf Port 6454 dient zum Senden UND Empfangen:
    // Nodes antworten laut Spec an Port 6454, und manche ignorieren Polls von
    // anderen Quellports. SO_REUSEADDR/SO_REUSEPORT erlaubt, dass andere
    // Art-Net-Software auf demselben Rechner parallel lauft.
    //
    // Poll-Ziele: Broadcast 255.255.255.255 UND der konfigurierte Art-Net-Ziel-
    // IP (per Unicast). Letzteres findet Nodes auch dort, wo Broadcasts nicht
    // ankommen (WLAN-AP, manche Switches/VLANs).
    class NodeDiscovery {
    public:
        // target_provider liefert die aktuell konfigurierte Ziel-IP (threadsafe).
        // port ist nur fuer Tests aenderbar.
        explicit NodeDiscovery(std::function<std::string()> target_provider,
                               uint16_t port = luminary::core::ARTNET_PORT,
                               std::string broadcast_address = "255.255.255.255");
        ~NodeDiscovery();

        NodeDiscovery(const NodeDiscovery&) = delete;
        NodeDiscovery& operator=(const NodeDiscovery&) = delete;

        void start();
        void stop();

        // Loest sofort einen ArtPoll aus (asynchron, Antworten kommen spaeter).
        void poll_now();

        // Snapshot der bekannten Nodes (ohne abgelaufene). Thread-safe.
        std::vector<luminary::core::ArtNetNode> nodes() const;

        // Letzte Fehlermeldung (z.B. Bind fehlgeschlagen), leer wenn ok.
        std::string last_error() const;

        static constexpr int POLL_INTERVAL_MS = 3000;   // Spec: ca. alle 2,5-3 s
        static constexpr int NODE_TIMEOUT_MS = 12000;   // ~4 verpasste Polls

    private:
        std::function<std::string()> target_provider_;
        uint16_t port_;
        std::string broadcast_address_;

        std::thread thread_;
        std::atomic<bool> running_{false};
        std::atomic<bool> poll_requested_{false};
        luminary::util::socket_t fd_ = luminary::util::INVALID_SOCK;

        mutable std::mutex mutex_;
        std::vector<luminary::core::ArtNetNode> nodes_;
        std::string last_error_;

        void run();
        void send_poll();
        void handle_datagram(const uint8_t* data, size_t size, const std::string& source_ip);
        void set_error(const std::string& message);
    };

} // namespace luminary::net
