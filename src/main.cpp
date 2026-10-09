#include "config/config.h"
#include "core/engine/engine.h"
#include "net/socket_server.h"
#include "net/http_server.h"
#include "net/node_discovery.h"
#include "net/remote_bridge.h"
#include "util/log.h"
#include "ofl/lexer.h"
#include "ofl/parser.h"
#include "ofl/resolver.h"
#include "ofl/validator.h"
#include "ofl/builder.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>
#include <set>
#include <memory>

namespace {
    // --wait-for-target: nichts senden, bis ein Client das Art-Net-Ziel gesetzt hat.
    bool g_wait_for_target = false;

    // Wird vom Signal-Handler gesetzt (SIGINT/SIGTERM), damit die main-Loop
    // sauber beendet statt der Prozess hart abgewuergt wird.
    std::atomic<bool> g_running{true};

    void handle_signal(int /*signal*/) {
        g_running = false;
    }

    void parse_args(int argc, char** argv) {
        for (int i = 1; i < argc; i++) {
            const std::string arg = argv[i];
            if (arg == "-l" || arg == "--log") {
                luminary::util::g_verbose_logging = true;
            } else if (arg == "--wait-for-target") {
                g_wait_for_target = true;
            }
        }
    }
}

int main(int argc, char** argv) {
    parse_args(argc, argv);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    Config cfg = load_config();
    luminary::core::Engine engine(cfg.engine);

    // User fixtures live in the per-user config directory. Seed bundled
    // OFL definitions on first run so the engine is usable even when it is
    // launched without Electron. User copies are never overwritten.
    const auto bundled_fixture_dir = std::filesystem::path(argv[0]).parent_path() / "fixtures";
    for (const auto& search_path : cfg.fixtures.search_paths) {
        const auto user_fixture_dir = std::filesystem::path(search_path);
        std::error_code ec;
        std::filesystem::create_directories(user_fixture_dir, ec);
        if (ec) continue;
        if (std::filesystem::exists(bundled_fixture_dir)) {
            for (const auto& entry : std::filesystem::directory_iterator(bundled_fixture_dir, ec)) {
                if (ec || !entry.is_regular_file() || entry.path().extension() != ".ofl") continue;
                const auto destination = user_fixture_dir / entry.path().filename();
                if (!std::filesystem::exists(destination)) {
                    std::filesystem::copy_file(entry.path(), destination, std::filesystem::copy_options::skip_existing, ec);
                }
            }
        }
        break;
    }

    // Load the complete OFL fixture catalog from the user fixture directory.
    // The first valid fixture/mode is also installed as the initial engine patch
    // so the standalone engine remains immediately usable.
    std::vector<std::pair<std::string, std::shared_ptr<ofl::Fixture>>> fixture_catalog;
    std::set<std::string> seen_fixture_names;

    for (const auto& search_path : cfg.fixtures.search_paths) {
        const auto dir = std::filesystem::path(search_path);
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec || !entry.is_regular_file() || entry.path().extension() != ".ofl") continue;
            try {
                std::ifstream file(entry.path());
                if (!file) continue;
                std::ostringstream ss; ss << file.rdbuf();
                Lexer lexer(ss.str());
                auto tokens = lexer.tokenize();
                ofl::Parser parser(tokens, entry.path().filename().string());
                auto node = parser.parse();
                ofl::Resolver resolver({entry.path().parent_path()});
                resolver.resolve(node);
                ofl::Validator validator;
                validator.validate(node);
                auto built = std::make_shared<ofl::Fixture>(ofl::Builder::build(node));
                const std::string key = entry.path().filename().string();
                if (seen_fixture_names.insert(key).second) {
                    fixture_catalog.emplace_back(key, std::move(built));
                }
            } catch (const std::exception& e) {
                luminary::util::log("OFL: " + entry.path().filename().string() + ": " + e.what());
            }
        }
        break;
    }

    if (fixture_catalog.empty()) {
        std::cerr << "Keine gueltige OFL-Fixture gefunden.\n";
        return 1;
    }

    // Keep a real OFL object in the engine patch. The selected mode is currently
    // represented by the first mode; the IPC catalog exposes all modes to the UI.
    engine.set_fixture_catalog(fixture_catalog);
    const auto& initial_fixture = fixture_catalog.front().second;
    engine.patch().add(initial_fixture, 0, DmxAddress(1));

    if (g_wait_for_target) engine.set_output_enabled(false);
    engine.refresh_patched_universes();
    engine.start();
    luminary::util::log("Engine gestartet (Refresh-Rate: " +
        std::to_string(cfg.engine.refresh_rate_hz) + " Hz)");

#ifdef _WIN32
    // Windows AF_UNIX-Sockets nutzen ebenfalls einen Pfad im Dateisystem.
    const std::string socket_path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : "C:\\Temp") + "\\luminary.sock";
#else
    const std::string socket_path = "/tmp/luminary.sock";
#endif

    luminary::net::PinAuth pin_auth;

    // Art-Net-Discovery: ArtPoll senden, ArtPollReply der Nodes einsammeln.
    // Ein Fehler hier (z.B. Port 6454 belegt) darf die Engine nicht stoppen.
    luminary::net::NodeDiscovery discovery([&engine] { return engine.artnet_target(); });
    discovery.start();
    if (!discovery.last_error().empty()) {
        std::cerr << "Node-Discovery nicht verfuegbar: " << discovery.last_error() << "\n";
    } else {
        luminary::util::log("Node-Discovery gestartet (ArtPoll alle " +
            std::to_string(luminary::net::NodeDiscovery::POLL_INTERVAL_MS / 1000) + " s)");
    }

    luminary::net::RemoteBridge bridge;   // Desktop <-> PWA: Show-Daten und Handy-Ereignisse

    luminary::net::SocketServer socket_server(engine, pin_auth, socket_path);
    socket_server.set_discovery(&discovery);
    socket_server.set_bridge(&bridge);
    socket_server.start();
    luminary::util::log("Socket-Server gestartet auf " + socket_path);

    // Getrennter, isolierter HTTP-Server fuers Mobile-Pairing - eigener
    // Port, eigene Authentifizierung (siehe pin_auth.h/http_server.h).
    constexpr uint16_t HTTP_PORT = 7346;
    luminary::net::HttpServer http_server(engine, pin_auth, HTTP_PORT);
    http_server.set_bridge(&bridge);
    http_server.start();
    luminary::util::log("HTTP-Server (Mobile-Pairing) gestartet auf Port " + std::to_string(HTTP_PORT));

    std::cout << "luminary laeuft (" << socket_path << ", HTTP-Port " << HTTP_PORT << "). Strg+C zum Beenden.\n";

    // Fail-Safe-Waechter: Ist die Desktop-App weg (Absturz, kill), obwohl sie
    // Fail-Safe-Kanaele (Nebel) gemeldet hat, gehen diese nach einer Schonfrist auf 0.
    // Ein Neustart der App im Fenster dieser Frist setzt nichts zurueck.
    constexpr auto FAILSAFE_GRACE = std::chrono::seconds(8);
    bool had_client = false, failsafe_fired = false;
    auto no_client_since = std::chrono::steady_clock::now();
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto now = std::chrono::steady_clock::now();
        if (socket_server.client_count() > 0) {
            had_client = true;
            failsafe_fired = false;
            no_client_since = now;
        } else if (had_client && !failsafe_fired && now - no_client_since > FAILSAFE_GRACE) {
            engine.apply_failsafe();
            failsafe_fired = true;
            luminary::util::log("Desktop-App seit " + std::to_string(FAILSAFE_GRACE.count()) + " s nicht verbunden - Fail-Safe-Kanaele (Nebel) auf 0");
        }
    }

    std::cout << "\nBeende...\n";
    http_server.stop();
    socket_server.stop();
    discovery.stop();
    engine.stop();

    return 0;
}
