//
// Created by janwin443 on 6/30/26.
//

#include "udp.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring> // Für std::memset, falls benötigt

using luminary::core::UDPManager;

int UDPManager::send(const uint8_t *buffer, const size_t size, const char *ipAddress, uint16_t port) {

    const int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "[UDPManager::send] Error creating socket" << std::endl;
        return -1;
    }

    // Korrektur 1: Struktur zwingend nullen
    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ipAddress, &destAddr.sin_addr) <= 0) {
        std::cerr << "[UDPManager::send] Invalid IP address" << std::endl;
        close(sock);
        return -1;
    }

    // Korrektur 2: Korrekten Statuscode bei sendto-Fehler zurückgeben
    if (const ssize_t sentBytes = sendto(sock, buffer, size, 0, reinterpret_cast<const sockaddr *>(&destAddr), sizeof(destAddr)); sentBytes < 0) {
        std::cerr << "[UDPManager::send] Error writing to socket" << std::endl;
        close(sock);
        return -1;
    }

    close(sock);
    return 0;
}