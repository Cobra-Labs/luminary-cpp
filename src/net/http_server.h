#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/engine/engine.h"
#include "pin_auth.h"
#include "remote_bridge.h"

namespace luminary::net {

    // Isolierter HTTP-Server fuer den Mobile-Zugriff - bewusst ein
    // GETRENNTER Codepfad vom lokalen Unix-Socket (SocketServer), nicht
    // nur ein weiterer Port auf demselben Server. Grund: der Unix-Socket
    // ist implizit vertrauenswuerdig (nur lokale Prozesse kommen dran),
    // dieser HTTP-Server ist ueber das Netzwerk erreichbar und braucht
    // deshalb eigene Authentifizierung (PIN-Pairing, siehe PinAuth) -
    // diese Trennung verhindert, dass ein Bug in der einen Schicht
    // versehentlich unauthentifizierten Zugriff auf die andere eroeffnet.
    //
    // Endpunkte:
    //   POST /api/pair      body: pin=XXXXXX          -> Token oder 401
    //   GET  /api/ping       (Auth noetig)             -> "PONG"
    //   POST /api/set        body: universe=..&channel=..&value=..  -> "OK"
    //   POST /api/set16      body: universe=..&msb=..&lsb=..&value=0..65535 -> "OK"
    //   POST /api/blackout   body: universe=..         -> "OK"
    //   GET  /api/state?rev=<showRev>&liverev=<liveRev>&universes=0,1
    //                        -> JSON {showRev, show, liveRev, live, values}
    //   POST /api/action     body: type=cue|preset&id=.. | type=fog&on=0|1[&flow=..&heat=..]
    //                              | type=blackout&on=0|1                  -> "OK"
    //   Alle ausser /api/pair verlangen: Authorization: Bearer <token>
    //
    // Kein TLS - siehe Kommentar in pin_auth.h zur bewussten Entscheidung.
    class HttpServer {
    public:
        HttpServer(luminary::core::Engine& engine, PinAuth& auth, uint16_t port);
        ~HttpServer();

        HttpServer(const HttpServer&) = delete;
        HttpServer& operator=(const HttpServer&) = delete;

        void start();
        void stop();

        // Optional: Briefkasten fuer Desktop <-> PWA (siehe RemoteBridge).
        void set_bridge(RemoteBridge* bridge) { bridge_ = bridge; }

    private:
        RemoteBridge* bridge_ = nullptr;
        luminary::core::Engine& engine_;
        PinAuth& auth_;
        uint16_t port_;

        std::thread accept_thread_;
        std::atomic<bool> running_{false};
        int listen_fd_ = -1;

        std::mutex clients_mutex_;
        std::vector<int> client_fds_;
        std::vector<std::thread> client_threads_;
        std::vector<std::thread::id> finished_threads_;   // fertige Threads, die noch gejoint werden muessen

        void accept_loop();
        void handle_client(int client_fd);
        void handle_client_impl(int client_fd);
        void reap_finished_threads();
        void remove_client_fd(int fd);
    };

} // namespace luminary::net
