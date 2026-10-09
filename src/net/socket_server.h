#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/engine/engine.h"
#include "pin_auth.h"
#include "node_discovery.h"
#include "remote_bridge.h"

namespace luminary::net {

    // Lokaler IPC-Server für die Engine, über Unix-Domain-Socket (AF_UNIX).
    //
    // Warum Unix-Socket statt TCP-Loopback:
    //  - läuft über einen Dateisystem-Pfad statt einen Port -> kein
    //    Port-Konflikt-Risiko mit anderer lokaler Software
    //  - Dateisystem-Permissions als zusätzliche Zugriffskontrolle
    //  - AF_UNIX wird seit Windows 10 1803 nativ unterstützt, damit
    //    reicht EIN Code-Pfad für Linux/Mac/Windows statt getrennter
    //    Named-Pipe (Windows) + Unix-Socket (Linux/Mac) Implementierungen.
    //
    // Mehrfach-Client-fähig: jede Verbindung läuft in ihrem eigenen Thread.
    // Das ist Voraussetzung dafür, dass später mehrere Komponenten
    // gleichzeitig verbunden sein können (z.B. Electron-Main-Process +
    // ein separates Cue-Steuerungs-Tool), ohne dass eine Verbindung die
    // andere blockiert.
    //
    // Protokoll (ein Befehl pro Zeile, abgeschlossen mit \n):
    //   PING                              -> PONG
    //   SET <universe> <channel> <value>  -> OK
    //   BLACKOUT <universe>               -> OK
    //   BLACKOUT_ALL                      -> OK
    //   INFO                              -> key=value summary
    //   FIXTURES                          -> JSON fixture/mode/channel catalog
    //   PATCH <universe> <addr> <fixtureId> -> "OK <entry-id>" oder ERR
    //   UNPATCH <entry-id>                -> OK oder ERR
    //   GET_PATCHES                       -> JSON Liste aktiver Patches
    //   CLEAR_PATCHES                     -> alle Patches entfernen (kein Blackout!)
    //   SET_SHOW <json>                   -> OK  (statische Daten fuer die PWA)
    //   SET_LIVE <json>                   -> OK  (fluechtiger Zustand fuer die PWA)
    //   SET_FAILSAFE <u:c> <u:c> ...      -> OK  (Kanaele, die bei Absturz/Beenden auf 0 gehen)
    //   APPLY_FAILSAFE                    -> OK  (Fail-Safe-Kanaele sofort auf 0)
    //   ACTIVE_UNIVERSES                  -> OK [0,1]  (Universes, die gerade gesendet werden)
    //   GET_VALUES <universe>             -> OK [512 Werte]
    //   EVENTS                            -> OK [json, ...]  (Handy-Aktionen, leert die Warteschlange)
    //   NODES                             -> JSON Liste per ArtPollReply gefundener Art-Net-Nodes
    //   POLL                              -> sofort neuen ArtPoll senden (OK)
    //   GEN_PIN                           -> neuer 6-stelliger Pairing-PIN
    //                                        fürs Mobile-HTTP-Interface
    //                                        (siehe HttpServer/PinAuth)
    // Bei Fehlern: ERR <nachricht>
    class SocketServer {
    public:
        SocketServer(luminary::core::Engine& engine, PinAuth& auth, std::string socket_path);
        ~SocketServer();

        SocketServer(const SocketServer&) = delete;
        SocketServer& operator=(const SocketServer&) = delete;

        void start();
        void stop();

        // Optional: Art-Net-Node-Discovery fuer die Befehle NODES/POLL.
        void set_discovery(NodeDiscovery* discovery) { discovery_ = discovery; }

        // Optional: Briefkasten fuer SET_SHOW/SET_LIVE/EVENTS (Desktop <-> PWA).
        void set_bridge(RemoteBridge* bridge) { bridge_ = bridge; }

        // Anzahl aktuell verbundener Desktop-Clients (fuer den Fail-Safe-Waechter in main.cpp).
        size_t client_count() {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            return client_fds_.size();
        }

    private:
        NodeDiscovery* discovery_ = nullptr;
        RemoteBridge* bridge_ = nullptr;
        luminary::core::Engine& engine_;
        PinAuth& auth_;
        std::string socket_path_;

        std::thread accept_thread_;
        std::atomic<bool> running_{false};
        int listen_fd_ = -1;

        // Aktive Client-Verbindungen. client_fds_ wird auch von den
        // Handler-Threads selbst gepflegt (Eintrag entfernen beim
        // Verbindungsende) - client_threads_ NUR vom accept_thread_
        // (hinzufügen) und stop() (entnehmen+joinen). Ein Thread verwaltet
        // sich also nie selbst in client_threads_, das wäre unsicher.
        std::mutex clients_mutex_;
        std::vector<int> client_fds_;
        std::vector<std::thread> client_threads_;

        void accept_loop();
        void handle_client(int client_fd);
        std::string handle_command(const std::string& line);

        void remove_client_fd(int fd);
        std::string fixture_catalog_json() const;
        std::string patches_json() const;
        std::string nodes_json() const;
    };

} // namespace luminary::net
