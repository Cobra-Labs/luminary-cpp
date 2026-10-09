//
// Created by janwin443 on 6/30/26.
//

#include "udp.h"
#include "util/sock_compat.h"
#include <iostream>

using luminary::core::UDPManager;
using namespace luminary::util;

int UDPManager::send(const uint8_t *buffer, const size_t size, const char *ipAddress, uint16_t port) {

    // Unter Windows muss Winsock initialisiert sein (referenzgezaehlt, einmalig reicht).
    static const bool winsock_ready = sock_init();
    if (!winsock_ready) {
        std::cerr << "[UDPManager::send] Winsock init failed" << std::endl;
        return -1;
    }

    const socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCK) {
        std::cerr << "[UDPManager::send] Error creating socket: "
                   << sock_error() << std::endl;
        return -1;
    }

    // WICHTIG: ohne SO_BROADCAST schlaegt sendto() an eine Broadcast-Adresse
    // (z.B. den Standard-Zielwert 255.255.255.255) auf Linux zuverlaessig
    // mit EACCES fehl - das war der eigentliche Grund fuer die dauerhaften
    // "Error writing to socket"-Meldungen.
    int broadcast_enable = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                    reinterpret_cast<const char*>(&broadcast_enable), sizeof(broadcast_enable)) < 0) {
        std::cerr << "[UDPManager::send] Error enabling SO_BROADCAST: "
                   << sock_error() << std::endl;
        sock_close(sock);
        return -1;
    }

    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ipAddress, &destAddr.sin_addr) <= 0) {
        std::cerr << "[UDPManager::send] Invalid IP address: " << ipAddress << std::endl;
        sock_close(sock);
        return -1;
    }

    if (const auto sentBytes = sendto(sock, reinterpret_cast<const char *>(buffer), static_cast<int>(size), 0,
            reinterpret_cast<const sockaddr *>(&destAddr), sizeof(destAddr));
        sentBytes < 0) {
        std::cerr << "[UDPManager::send] Error writing to socket: "
                   << sock_error() << std::endl;
        sock_close(sock);
        return -1;
    }

    sock_close(sock);
    return 0;
}