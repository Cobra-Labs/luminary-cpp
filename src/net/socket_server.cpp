#include "socket_server.h"
#include "util/log.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <set>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    #include <afunix.h>   // AF_UNIX auf Windows, seit Windows 10 1803
    #pragma comment(lib, "ws2_32.lib")
    #define LUMINARY_CLOSESOCK closesocket
#else
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <unistd.h>
    #define LUMINARY_CLOSESOCK close
#endif

using namespace luminary::net;
using namespace luminary::util;

SocketServer::SocketServer(luminary::core::Engine& engine, PinAuth& auth, std::string socket_path)
    : engine_(engine), auth_(auth), socket_path_(std::move(socket_path)) {}

SocketServer::~SocketServer() {
    stop();
}

void SocketServer::start() {
    if (running_) return;
    running_ = true;
    accept_thread_ = std::thread(&SocketServer::accept_loop, this);
}

void SocketServer::stop() {
    if (!running_) return;
    running_ = false;

    // Listen-Socket aufwecken (siehe Kommentar in accept_loop weiter unten
    // zu shutdown() vs. close()).
    if (listen_fd_ != -1) {
#ifndef _WIN32
        shutdown(listen_fd_, SHUT_RDWR);
#endif
        LUMINARY_CLOSESOCK(listen_fd_);
        listen_fd_ = -1;
    }

    // Alle aktiven Client-Verbindungen aufwecken, damit ihre jeweiligen
    // recv()-Aufrufe in handle_client() zurückkehren, statt fuer immer zu
    // haengen.
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (int fd : client_fds_) {
#ifndef _WIN32
            shutdown(fd, SHUT_RDWR);
#endif
        }
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    // Client-Threads entnehmen und joinen - AUSSERHALB des Locks, sonst
    // Deadlock: ein Handler-Thread braucht clients_mutex_ kurz selbst,
    // um sich beim Verbindungsende aus client_fds_ auszutragen.
    std::vector<std::thread> threads_to_join;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        threads_to_join = std::move(client_threads_);
        client_threads_.clear();
    }
    for (auto& t : threads_to_join) {
        if (t.joinable()) t.join();
    }

    // Socket-Datei aufräumen, damit ein Neustart nicht an einer
    // verwaisten Datei scheitert (siehe auch unlink() vor bind() unten).
#ifndef _WIN32
    std::remove(socket_path_.c_str());
#endif
}

void SocketServer::accept_loop() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        log("SocketServer: WSAStartup fehlgeschlagen");
        running_ = false;
        return;
    }
#endif

    listen_fd_ = static_cast<int>(socket(AF_UNIX, SOCK_STREAM, 0));
    if (listen_fd_ < 0) {
        log("SocketServer: socket() fehlgeschlagen");
        running_ = false;
        return;
    }

#ifndef _WIN32
    // Verwaiste Socket-Datei von einem vorherigen, nicht sauber beendeten
    // Lauf entfernen - sonst schlägt bind() mit "Address already in use" fehl.
    std::remove(socket_path_.c_str());
#endif

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(addr.sun_path)) {
        log("SocketServer: Socket-Pfad zu lang: " + socket_path_);
        LUMINARY_CLOSESOCK(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return;
    }
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", socket_path_.c_str());

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log("SocketServer: bind() fehlgeschlagen auf " + socket_path_);
        LUMINARY_CLOSESOCK(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return;
    }

    // Backlog von 8 statt 1 - erlaubt mehrere Verbindungsversuche in
    // kurzer Zeit, statt sie abzuweisen, waehrend accept() beschaeftigt ist.
    if (listen(listen_fd_, 8) < 0) {
        log("SocketServer: listen() fehlgeschlagen");
        LUMINARY_CLOSESOCK(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return;
    }

    log("SocketServer: horcht auf " + socket_path_);

    while (running_) {
        int client_fd = static_cast<int>(accept(listen_fd_, nullptr, nullptr));

        if (client_fd < 0) {
            // Wenn stop() gerade den listen_fd_ geschlossen hat, landen wir
            // hier normal - kein echter Fehler, einfach die Schleife verlassen.
            if (!running_) break;
            continue;
        }

        log("SocketServer: Client verbunden");

        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.push_back(client_fd);
            client_threads_.emplace_back(&SocketServer::handle_client, this, client_fd);
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

void SocketServer::remove_client_fd(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = std::find(client_fds_.begin(), client_fds_.end(), fd);
    if (it != client_fds_.end()) {
        client_fds_.erase(it);
    }
}

void SocketServer::handle_client(int client_fd) {
    std::string buffer;
    char chunk[512];

    while (running_) {
        int n = recv(client_fd, chunk, sizeof(chunk), 0);
        if (n <= 0) break; // Verbindung geschlossen, Fehler, oder von stop() aufgeweckt

        buffer.append(chunk, static_cast<size_t>(n));

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            std::string response = handle_command(line) + "\n";
            send(client_fd, response.c_str(), static_cast<int>(response.size()), 0);
        }
    }

    LUMINARY_CLOSESOCK(client_fd);
    remove_client_fd(client_fd);
    log("SocketServer: Client getrennt");
}


namespace {
std::string trim_copy(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}
std::string json_escape(const std::string& in) {
    std::ostringstream out;
    for (unsigned char c : in) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
                else out << c;
        }
    }
    return out.str();
}
std::string cap_json(const ofl::Capability& cap) {
    std::ostringstream o;
    o << "{\"start\":" << cap.range_start << ",\"end\":" << cap.range_end
      << ",\"type\":\"" << json_escape(ofl::cap_type_to_string(cap.cap_type)) << "\",\"label\":\""
      << json_escape(cap.label) << "\",\"params\":{";
    bool first=true; for (const auto& [k,v] : cap.params) { if(!first)o<<','; first=false; o<<"\""<<json_escape(k)<<"\":\""<<json_escape(v)<<"\""; }
    o << "}}"; return o.str();
}
}

std::string SocketServer::fixture_catalog_json() const {
    // The engine currently keeps its OFL fixtures in the patch object. Expose
    // every unique fixture represented there. Electron also keeps the source
    // .ofl files in the same per-user fixture directory.
    std::ostringstream o; o << "{\"fixtures\":[";
    bool firstFixture=true;
    std::set<std::string> seen;
    for (const auto& [filename, fixture] : engine_.fixture_catalog()) {
        if (!fixture || !seen.insert(filename).second) continue;
        if (!firstFixture) o << ',';
        firstFixture = false;
        const auto& f=*fixture;
        const auto stable_id = filename.substr(0, filename.size() - 4);
        o << "{\"id\":\"" << json_escape(stable_id) << "\",\"sourceFile\":\"" << json_escape(filename) << "\",\"name\":\"" << json_escape(f.name)
          << "\",\"model\":\"" << json_escape(f.name) << "\",\"manufacturer\":\"" << json_escape(f.manufacturer) << "\",\"type\":\""
          << json_escape(ofl::fixture_type_to_string(f.fixture_type)) << "\",\"channels\":[";
        for (size_t i=0;i<f.channels.size();++i) { const auto& c=f.channels[i]; if(i)o<<',';
            o<<"{\"name\":\""<<json_escape(c.name)<<"\",\"type\":\""<<json_escape(ofl::attribute_to_string(c.attribute))
             <<"\",\"resolution\":"<<c.resolution<<",\"defaultValue\":"<<c.default_value<<",\"capabilities\":[";
            for(size_t j=0;j<c.capabilities.size();++j){if(j)o<<',';o<<cap_json(c.capabilities[j]);} o<<"]}";
        }
        o << "],\"modes\":[";
        for(size_t i=0;i<f.modes.size();++i){const auto&m=f.modes[i];if(i)o<<',';o<<"{\"name\":\""<<json_escape(m.name)<<"\",\"channelCount\":"<<m.channel_count<<",\"mappings\":{";
            bool first=true; for(const auto& [slot,name]:m.mappings){if(!first)o<<',';first=false;o<<"\""<<slot<<"\":\""<<json_escape(name)<<"\"";} o<<"}}";
        }
        o << "]}";
    }
    o << "]}"; return o.str();
}

std::string SocketServer::patches_json() const {
    std::ostringstream o; o << "{\"patches\":[";
    bool first = true;
    for (const auto& entry : engine_.patch().entries()) {
        if (!first) o << ',';
        first = false;

        // fixture-Pointer gegen den Katalog matchen, um die stabile ID
        // (Dateiname ohne .ofl) zurückzugeben - PatchEntry selbst kennt
        // nur das geparste Fixture-Objekt, nicht dessen Ursprungsdatei.
        std::string fixture_id;
        for (const auto& [filename, fixture] : engine_.fixture_catalog()) {
            if (fixture.get() == entry.fixture.get()) {
                fixture_id = filename.substr(0, filename.size() - 4);
                break;
            }
        }

        const int channel_count = entry.fixture->modes.empty()
            ? 1
            : entry.fixture->modes[static_cast<size_t>(entry.mode_index)].channel_count;
        const std::string mode_name = entry.fixture->modes.empty()
            ? ""
            : entry.fixture->modes[static_cast<size_t>(entry.mode_index)].name;

        o << "{\"id\":\"" << json_escape(entry.id) << "\",\"universe\":" << entry.universe_id
          << ",\"address\":" << entry.start_address.value
          << ",\"fixtureId\":\"" << json_escape(fixture_id) << "\""
          << ",\"fixtureName\":\"" << json_escape(entry.fixture->name) << "\""
          << ",\"modeName\":\"" << json_escape(mode_name) << "\""
          << ",\"channelCount\":" << channel_count << "}";
    }
    o << "]}"; return o.str();
}

std::string SocketServer::nodes_json() const {
    std::ostringstream o;
    o << "{\"listening\":" << (discovery_ ? "true" : "false") << ",\"error\":\""
      << json_escape(discovery_ ? discovery_->last_error() : "Discovery nicht aktiv") << "\",\"nodes\":[";
    if (discovery_) {
        using namespace std::chrono;
        const int64_t now = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        bool first = true;
        for (const auto& n : discovery_->nodes()) {
            if (!first) o << ',';
            first = false;
            o << "{\"ip\":\"" << json_escape(n.ip) << "\",\"mac\":\"" << json_escape(n.mac) << "\""
              << ",\"shortName\":\"" << json_escape(n.short_name) << "\""
              << ",\"longName\":\"" << json_escape(n.long_name) << "\""
              << ",\"report\":\"" << json_escape(n.node_report) << "\""
              << ",\"firmware\":" << n.firmware << ",\"oem\":" << n.oem
              << ",\"style\":" << static_cast<int>(n.style) << ",\"bindIndex\":" << static_cast<int>(n.bind_index)
              << ",\"ageMs\":" << (now - n.last_seen_ms) << ",\"ports\":[";
            for (size_t i = 0; i < n.ports.size(); ++i) {
                if (i) o << ',';
                o << "{\"direction\":\"" << (n.ports[i].output ? "out" : "in") << "\",\"universe\":" << n.ports[i].universe
                  << ",\"active\":" << (n.ports[i].data_active ? "true" : "false") << "}";
            }
            o << "]}";
        }
    }
    o << "]}";
    return o.str();
}

std::string SocketServer::handle_command(const std::string& line) {
    log("SocketServer: Befehl empfangen: " + line);

    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    try {
        if (cmd == "GEN_PIN") {
            return "OK " + auth_.generate_pin();
        }

        if (cmd == "PING") {
            return "PONG";
        }

        if (cmd == "FIXTURES") {
            return "OK " + fixture_catalog_json();
        }

        if (cmd == "SET_SHOW" || cmd == "SET_LIVE") {
            if (!bridge_) return "ERR remote bridge not available";
            // Der Rest der Zeile ist ein einzeiliges JSON-Objekt (kann Leerzeichen enthalten).
            const size_t space = line.find(' ');
            const std::string json = space == std::string::npos ? "" : line.substr(space + 1);
            const bool ok = cmd == "SET_SHOW" ? bridge_->set_show(json) : bridge_->set_live(json);
            return ok ? "OK" : "ERR expected a single-line JSON object";
        }

        if (cmd == "SET_FAILSAFE") {
            // Kanaele "universe:channel", durch Leerzeichen getrennt (leer = keine).
            std::vector<std::pair<int, int>> channels;
            std::string token;
            while (iss >> token) {
                const size_t colon = token.find(':');
                if (colon == std::string::npos) return "ERR usage: SET_FAILSAFE <universe:channel> ...";
                try {
                    const int u = std::stoi(token.substr(0, colon));
                    const int c = std::stoi(token.substr(colon + 1));
                    if (u < 0 || u >= engine_.universe_count() || c < 1 || c > 512) return "ERR channel out of range: " + token;
                    channels.emplace_back(u, c);
                } catch (...) { return "ERR invalid channel: " + token; }
                if (channels.size() > 256) return "ERR too many channels";
            }
            engine_.set_failsafe_channels(std::move(channels));
            return "OK";
        }

        if (cmd == "APPLY_FAILSAFE") {
            // Die Desktop-App ruft das beim Beenden auf (auch unter Windows, wo ein
            // kill() der Engine keine Chance zum sauberen Abschalten laesst).
            engine_.apply_failsafe();
            return "OK";
        }

        if (cmd == "ACTIVE_UNIVERSES") {
            std::string out = "OK [";
            bool first = true;
            for (int u : engine_.active_universes()) { if (!first) out += ','; first = false; out += std::to_string(u); }
            return out + "]";
        }

        if (cmd == "GET_VALUES") {
            // Aktuelle Kanalwerte einer Universe (512 Zahlen). Die Desktop-App gleicht
            // damit beim Verbinden ihre Anzeige mit dem ab, was wirklich ausgegeben wird.
            int universe = 0;
            iss >> universe;
            const auto raw = engine_.universe(universe).raw();
            std::string out = "OK [";
            for (size_t i = 0; i < raw.size(); ++i) { if (i) out += ','; out += std::to_string(static_cast<int>(raw[i])); }
            return out + "]";
        }

        if (cmd == "EVENTS") {
            if (!bridge_) return "OK []";
            std::string out = "OK [";
            bool first = true;
            for (const auto& e : bridge_->drain_events()) {
                if (!first) out += ',';
                first = false;
                out += e;
            }
            return out + "]";
        }

        if (cmd == "NODES") {
            return "OK " + nodes_json();
        }

        if (cmd == "POLL") {
            if (!discovery_) return "ERR node discovery not running";
            discovery_->poll_now();
            return "OK";
        }

        if (cmd == "GET_PATCHES") {
            return "OK " + patches_json();
        }

        if (cmd == "CLEAR_PATCHES") {
            engine_.patch().clear_all();
            engine_.refresh_patched_universes();
            return "OK";
        }

        if (cmd == "PATCH") {
            int universe, address;
            std::string fixture_id;
            if (!(iss >> universe >> address >> fixture_id)) {
                return "ERR usage: PATCH <universe> <address> <fixtureId> [modeName]";
            }
            // Optionaler Mode-Name - Rest der Zeile, falls vorhanden (Mode-
            // Namen können Leerzeichen enthalten, z.B. "16-bit fine").
            std::string mode_name;
            std::getline(iss, mode_name);
            mode_name = trim_copy(mode_name);

            std::shared_ptr<ofl::Fixture> fixture;
            for (const auto& [filename, candidate] : engine_.fixture_catalog()) {
                const std::string stable_id = filename.substr(0, filename.size() - 4);
                if (stable_id == fixture_id) {
                    fixture = candidate;
                    break;
                }
            }
            if (!fixture) {
                return "ERR unknown fixtureId: " + fixture_id;
            }

            int mode_index = 0;
            if (!mode_name.empty()) {
                bool found = false;
                for (size_t i = 0; i < fixture->modes.size(); i++) {
                    if (fixture->modes[i].name == mode_name) {
                        mode_index = static_cast<int>(i);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return "ERR unknown mode '" + mode_name + "' for fixture '" + fixture_id + "'";
                }
            }

            // Patch::add wirft bei Kollision/Adressüberlauf/ungültigem
            // Mode-Index - vom äusseren catch(...) unten sauber in
            // "ERR <msg>" übersetzt.
            const std::string entry_id = engine_.patch().add(fixture, universe, DmxAddress(address), mode_index);
            engine_.refresh_patched_universes();
            return "OK " + entry_id;
        }

        if (cmd == "UNPATCH") {
            std::string entry_id;
            if (!(iss >> entry_id)) {
                return "ERR usage: UNPATCH <entry-id>";
            }

            const auto& entries = engine_.patch().entries();
            const bool existed = std::any_of(entries.begin(), entries.end(),
                [&entry_id](const auto& e) { return e.id == entry_id; });
            if (!existed) {
                return "ERR unknown patch id: " + entry_id;
            }

            engine_.patch().remove(entry_id);
            engine_.refresh_patched_universes();
            return "OK";
        }

        if (cmd == "INFO") {
            return "OK universe_count=" + std::to_string(engine_.universe_count()) +
                   " refresh_hz=" + std::to_string(engine_.refresh_rate_hz()) +
                   " grand_master=" + std::to_string(engine_.grand_master()) +
                   " artnet_target=" + engine_.artnet_target();
        }

        if (cmd == "SET_ARTNET_TARGET") {
            std::string target;
            if (!(iss >> target)) return "ERR usage: SET_ARTNET_TARGET <ipv4>";
            engine_.set_artnet_target(target);
            return "OK";
        }

        if (cmd == "SET_REFRESH") {
            int hz;
            if (!(iss >> hz)) return "ERR usage: SET_REFRESH <hz>";
            engine_.set_refresh_rate_hz(hz);
            return "OK";
        }

        if (cmd == "SET_MASTER") {
            int value;
            if (!(iss >> value)) return "ERR usage: SET_MASTER <0-255>";
            engine_.set_grand_master(value);
            return "OK";
        }

        if (cmd == "SET") {
            int universe, channel, value;
            if (!(iss >> universe >> channel >> value)) {
                return "ERR usage: SET <universe> <channel> <value>";
            }
            if (value < 0 || value > 255) {
                return "ERR value must be 0-255";
            }
            engine_.set_channel(universe, channel, static_cast<uint8_t>(value));
            return "OK";
        }

        if (cmd == "SET16") {
            int universe, msb_channel, lsb_channel, value;
            if (!(iss >> universe >> msb_channel >> lsb_channel >> value)) {
                return "ERR usage: SET16 <universe> <msb-channel> <lsb-channel> <value>";
            }
            if (value < 0 || value > 65535) {
                return "ERR 16-bit value must be 0-65535";
            }
            if (msb_channel < 1 || msb_channel > 512 || lsb_channel < 1 || lsb_channel > 512 || msb_channel == lsb_channel) {
                return "ERR invalid 16-bit DMX channel pair";
            }
            engine_.set_16(universe, msb_channel, lsb_channel, static_cast<uint16_t>(value));
            return "OK";
        }

        if (cmd == "BLACKOUT") {
            int universe;
            if (!(iss >> universe)) {
                return "ERR usage: BLACKOUT <universe>";
            }
            engine_.universe(universe).blackout();
            return "OK";
        }

        if (cmd == "BLACKOUT_ALL") {
            engine_.blackout();
            return "OK";
        }

        return "ERR unknown command: " + cmd;
    } catch (const std::exception& e) {
        return std::string("ERR ") + e.what();
    }
}
