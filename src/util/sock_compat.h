#pragma once
// Duenne Portabilitaetsschicht fuer UDP-Sockets (POSIX und Winsock).
// Bewusst klein gehalten: nur das, was udp.cpp und node_discovery.cpp brauchen.
// (socket_server.cpp/http_server.cpp haben ihre eigenen Windows-Zweige.)
#include <cerrno>
#include <cstring>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifndef SIO_UDP_CONNRESET
        #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
    #endif
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace luminary::util {

#ifdef _WIN32
    using socket_t = SOCKET;
    using sock_len_t = int;
    inline const socket_t INVALID_SOCK = INVALID_SOCKET;

    // WSAStartup ist referenzgezaehlt; mehrfaches Aufrufen ist unkritisch.
    inline bool sock_init() { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }
    inline void sock_close(socket_t s) { closesocket(s); }
    inline std::string sock_error() { return "WSA error " + std::to_string(WSAGetLastError()); }
    inline bool sock_set_nonblocking(socket_t s) { u_long on = 1; return ioctlsocket(s, FIONBIO, &on) == 0; }
    // Windows meldet ICMP "port unreachable" sonst als Fehler beim naechsten recvfrom().
    inline void sock_disable_udp_connreset(socket_t s) {
        DWORD off = FALSE, bytes = 0;
        WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &bytes, nullptr, nullptr);
    }
    // >0: lesbar, 0: Timeout, <0: Fehler
    inline int sock_wait_readable(socket_t s, int timeout_ms) {
        WSAPOLLFD p{s, POLLRDNORM, 0};
        const int r = WSAPoll(&p, 1, timeout_ms);
        return (r > 0 && (p.revents & (POLLRDNORM | POLLERR | POLLHUP))) ? 1 : r;
    }
#else
    using socket_t = int;
    using sock_len_t = socklen_t;
    inline const socket_t INVALID_SOCK = -1;

    inline bool sock_init() { return true; }
    inline void sock_close(socket_t s) { ::close(s); }
    inline std::string sock_error() { return std::strerror(errno); }
    inline bool sock_set_nonblocking(socket_t) { return true; }   // wir nutzen MSG_DONTWAIT
    inline void sock_disable_udp_connreset(socket_t) {}
    inline int sock_wait_readable(socket_t s, int timeout_ms) {
        pollfd p{s, POLLIN, 0};
        const int r = ::poll(&p, 1, timeout_ms);
        return (r > 0 && (p.revents & (POLLIN | POLLERR | POLLHUP))) ? 1 : r;
    }
#endif

#ifdef _WIN32
    constexpr int SOCK_RECV_FLAGS = 0;   // Socket ist per sock_set_nonblocking() nicht blockierend
#else
    constexpr int SOCK_RECV_FLAGS = MSG_DONTWAIT;
#endif

} // namespace luminary::util
