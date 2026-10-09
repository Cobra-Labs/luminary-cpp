#include "http_server.h"
#include "util/log.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define LUMINARY_CLOSESOCK closesocket
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #define LUMINARY_CLOSESOCK close
#endif

using namespace luminary::net;
using namespace luminary::util;

namespace {

    struct HttpRequest {
        std::string method;
        std::string path;
        std::map<std::string, std::string> headers; // Keys lowercase
        std::string body;
    };

    std::string to_lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    // Minimales application/x-www-form-urlencoded Parsing - reicht für
    // unsere simplen key=value&key2=value2 Bodies, kein voller RFC-3986-
    // Decoder (z.B. kein Multi-Byte-UTF8-Escaping), aber für PIN/Zahlen-
    // Werte, die wir hier erwarten, ausreichend.
    std::map<std::string, std::string> parse_form_body(const std::string& body) {
        std::map<std::string, std::string> result;
        std::istringstream iss(body);
        std::string pair;
        while (std::getline(iss, pair, '&')) {
            const size_t eq = pair.find('=');
            if (eq == std::string::npos) continue;
            std::string key = pair.substr(0, eq);
            std::string value = pair.substr(eq + 1);
            // %XX-Escapes und '+' als Leerzeichen decodieren
            std::string decoded;
            for (size_t i = 0; i < value.size(); i++) {
                if (value[i] == '+') {
                    decoded += ' ';
                } else if (value[i] == '%' && i + 2 < value.size()) {
                    const std::string hex = value.substr(i + 1, 2);
                    decoded += static_cast<char>(std::stoi(hex, nullptr, 16));
                    i += 2;
                } else {
                    decoded += value[i];
                }
            }
            result[key] = decoded;
        }
        return result;
    }

    // Liest genau eine HTTP-Anfrage von einem (blockierenden) Socket.
    // Gibt false zurueck, wenn die Verbindung geschlossen wurde oder die
    // Anfrage nicht als gueltiges HTTP geparst werden konnte.
    bool read_request(int client_fd, HttpRequest& out) {
        std::string buffer;
        char chunk[1024];

        // 1. Header-Bereich lesen, bis \r\n\r\n gefunden ist.
        size_t header_end;
        while ((header_end = buffer.find("\r\n\r\n")) == std::string::npos) {
            const int n = recv(client_fd, chunk, sizeof(chunk), 0);
            if (n <= 0) return false;
            buffer.append(chunk, static_cast<size_t>(n));
            if (buffer.size() > 16384) return false; // Grobe Obergrenze gegen Speicher-Missbrauch
        }

        const std::string header_block = buffer.substr(0, header_end);
        std::string rest_after_headers = buffer.substr(header_end + 4);

        std::istringstream header_stream(header_block);
        std::string line;

        // Request-Zeile: "METHOD /pfad HTTP/1.1"
        if (!std::getline(header_stream, line)) return false;
        line = trim(line);
        std::istringstream request_line(line);
        std::string version;
        if (!(request_line >> out.method >> out.path >> version)) return false;

        // Header-Zeilen
        while (std::getline(header_stream, line)) {
            line = trim(line);
            if (line.empty()) continue;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = to_lower(trim(line.substr(0, colon)));
            std::string value = trim(line.substr(colon + 1));
            out.headers[key] = value;
        }

        // 2. Body lesen, falls Content-Length gesetzt ist.
        size_t content_length = 0;
        auto it = out.headers.find("content-length");
        if (it != out.headers.end()) {
            content_length = static_cast<size_t>(std::stoul(it->second));
        }

        if (content_length > 1'000'000) return false; // Grobe Obergrenze

        while (rest_after_headers.size() < content_length) {
            const int n = recv(client_fd, chunk, sizeof(chunk), 0);
            if (n <= 0) return false;
            rest_after_headers.append(chunk, static_cast<size_t>(n));
        }
        out.body = rest_after_headers.substr(0, content_length);

        return true;
    }

    void send_response(int client_fd, int status_code, const std::string& status_text,
                        const std::string& body,
                        const std::string& content_type = "text/plain") {
        std::ostringstream oss;
        oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
            << "Content-Type: " << content_type << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            // CORS: diese API wird potenziell von einer PWA aufgerufen, die
            // von einem anderen Origin (z.B. Dev-Server-Port) laeuft.
            << "Access-Control-Allow-Origin: *\r\n"
            << "Access-Control-Allow-Headers: Authorization, Content-Type\r\n"
            << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            << "Connection: close\r\n"
            << "\r\n"
            << body;

        const std::string response = oss.str();
        send(client_fd, response.c_str(), static_cast<int>(response.size()), 0);
    }

} // namespace

HttpServer::HttpServer(luminary::core::Engine& engine, PinAuth& auth, uint16_t port)
    : engine_(engine), auth_(auth), port_(port) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    if (running_) return;
    running_ = true;
    accept_thread_ = std::thread(&HttpServer::accept_loop, this);
}

void HttpServer::stop() {
    if (!running_) return;
    running_ = false;

    if (listen_fd_ != -1) {
#ifndef _WIN32
        shutdown(listen_fd_, SHUT_RDWR);
#endif
        LUMINARY_CLOSESOCK(listen_fd_);
        listen_fd_ = -1;
    }

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

    std::vector<std::thread> threads_to_join;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        threads_to_join = std::move(client_threads_);
        client_threads_.clear();
    }
    for (auto& t : threads_to_join) {
        if (t.joinable()) t.join();
    }
}

void HttpServer::accept_loop() {
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        log("HttpServer: WSAStartup fehlgeschlagen");
        running_ = false;
        return;
    }
#endif

    listen_fd_ = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (listen_fd_ < 0) {
        log("HttpServer: socket() fehlgeschlagen");
        running_ = false;
        return;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // Auf allen Interfaces horchen - fuers LAN noetig
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        log("HttpServer: bind() fehlgeschlagen auf Port " + std::to_string(port_));
        LUMINARY_CLOSESOCK(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return;
    }

    if (listen(listen_fd_, 16) < 0) {
        log("HttpServer: listen() fehlgeschlagen");
        LUMINARY_CLOSESOCK(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return;
    }

    log("HttpServer: horcht auf Port " + std::to_string(port_) + " (alle Interfaces)");

    while (running_) {
        int client_fd = static_cast<int>(accept(listen_fd_, nullptr, nullptr));
        if (client_fd < 0) {
            if (!running_) break;
            continue;
        }

        reap_finished_threads();
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            client_fds_.push_back(client_fd);
            client_threads_.emplace_back(&HttpServer::handle_client, this, client_fd);
        }
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

void HttpServer::remove_client_fd(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = std::find(client_fds_.begin(), client_fds_.end(), fd);
    if (it != client_fds_.end()) {
        client_fds_.erase(it);
    }
}

// Jede Verbindung bekommt einen eigenen Thread (HTTP mit "Connection: close").
// Fertige Threads muessen gejoint werden, sonst bleiben ihre Stacks bis zum
// Serverende liegen - mit Polling aus der PWA waeren das Tausende.
void HttpServer::reap_finished_threads() {
    std::vector<std::thread> done;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto it = client_threads_.begin(); it != client_threads_.end();) {
            const auto id = it->get_id();
            const auto f = std::find(finished_threads_.begin(), finished_threads_.end(), id);
            if (f != finished_threads_.end()) {
                finished_threads_.erase(f);
                done.push_back(std::move(*it));
                it = client_threads_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& t : done) if (t.joinable()) t.join();
}

void HttpServer::handle_client(int client_fd) {
    handle_client_impl(client_fd);
    std::lock_guard<std::mutex> lock(clients_mutex_);
    finished_threads_.push_back(std::this_thread::get_id());
}

void HttpServer::handle_client_impl(int client_fd) {
    HttpRequest req;
    if (!read_request(client_fd, req)) {
        LUMINARY_CLOSESOCK(client_fd);
        remove_client_fd(client_fd);
        return;
    }

    // "/api/state?rev=3" -> Pfad und Query trennen, damit Routing nur den Pfad sieht.
    std::map<std::string, std::string> query;
    if (const size_t q = req.path.find('?'); q != std::string::npos) {
        query = parse_form_body(req.path.substr(q + 1));
        req.path.erase(q);
    }

    // Status-Polling der PWA wuerde das Log fluten.
    if (req.path != "/api/state" && req.path != "/api/ping") {
        log("HttpServer: " + req.method + " " + req.path);
    }

    // CORS-Preflight: Browser schicken hier bewusst KEINEN Authorization-
    // Header (das ist Teil des Preflight-Mechanismus selbst) - diesen
    // Request also VOR dem Auth-Check beantworten, sonst schlaegt jede
    // echte Anfrage von einer Web-basierten PWA schon am Preflight fehl.
    if (req.method == "OPTIONS") {
        send_response(client_fd, 204, "No Content", "");
        LUMINARY_CLOSESOCK(client_fd);
        remove_client_fd(client_fd);
        return;
    }

    // /api/pair ist der einzige Endpunkt ohne vorherige Authentifizierung -
    // das IST die Authentifizierung.
    if (req.method == "POST" && req.path == "/api/pair") {
        auto form = parse_form_body(req.body);
        const std::string pin = form.count("pin") ? form["pin"] : "";
        const std::string token = auth_.redeem_pin(pin);

        if (token.empty()) {
            send_response(client_fd, 401, "Unauthorized", "invalid or expired pin");
        } else {
            send_response(client_fd, 200, "OK", token);
        }
        LUMINARY_CLOSESOCK(client_fd);
        remove_client_fd(client_fd);
        return;
    }

    // Ab hier: alle Endpunkte brauchen ein gueltiges Token.
    std::string token;
    auto auth_it = req.headers.find("authorization");
    if (auth_it != req.headers.end()) {
        const std::string& value = auth_it->second;
        const std::string prefix = "Bearer ";
        if (value.rfind(prefix, 0) == 0) {
            token = value.substr(prefix.size());
        }
    }

    if (!auth_.is_valid_token(token)) {
        send_response(client_fd, 401, "Unauthorized", "missing or invalid token");
        LUMINARY_CLOSESOCK(client_fd);
        remove_client_fd(client_fd);
        return;
    }

    try {
        if (req.method == "GET" && req.path == "/api/ping") {
            send_response(client_fd, 200, "OK", "PONG");
        } else if (req.method == "POST" && req.path == "/api/set") {
            auto form = parse_form_body(req.body);
            if (!form.count("universe") || !form.count("channel") || !form.count("value")) {
                send_response(client_fd, 400, "Bad Request",
                               "ERR usage: universe, channel, value required");
            } else {
                const int universe = std::stoi(form["universe"]);
                const int channel = std::stoi(form["channel"]);
                const int value = std::stoi(form["value"]);
                if (value < 0 || value > 255) {
                    send_response(client_fd, 400, "Bad Request", "ERR value must be 0-255");
                } else {
                    engine_.set_channel(universe, channel, static_cast<uint8_t>(value));
                    if (bridge_) {
                        bridge_->push_event("{\"t\":\"set\",\"u\":" + std::to_string(universe) + ",\"c\":" + std::to_string(channel) +
                                            ",\"v\":" + std::to_string(value) + "}",
                                            "set:" + std::to_string(universe) + ":" + std::to_string(channel));
                    }
                    send_response(client_fd, 200, "OK", "OK");
                }
            }
        } else if (req.method == "POST" && req.path == "/api/blackout") {
            auto form = parse_form_body(req.body);
            if (!form.count("universe")) {
                send_response(client_fd, 400, "Bad Request", "ERR usage: universe required");
            } else {
                const int universe = std::stoi(form["universe"]);
                engine_.universe(universe).blackout();
                send_response(client_fd, 200, "OK", "OK");
            }
        } else if (req.method == "POST" && req.path == "/api/set16") {
            auto form = parse_form_body(req.body);
            if (!form.count("universe") || !form.count("msb") || !form.count("lsb") || !form.count("value")) {
                send_response(client_fd, 400, "Bad Request", "ERR usage: universe, msb, lsb, value required");
            } else {
                const int universe = std::stoi(form["universe"]);
                const int msb = std::stoi(form["msb"]);
                const int lsb = std::stoi(form["lsb"]);
                const int value = std::stoi(form["value"]);
                if (value < 0 || value > 65535 || msb < 1 || msb > 512 || lsb < 1 || lsb > 512 || msb == lsb) {
                    send_response(client_fd, 400, "Bad Request", "ERR invalid 16-bit value or channel pair");
                } else {
                    engine_.set_16(universe, msb, lsb, static_cast<uint16_t>(value));
                    if (bridge_) {
                        const std::string u = std::to_string(universe);
                        bridge_->push_event("{\"t\":\"set\",\"u\":" + u + ",\"c\":" + std::to_string(msb) + ",\"v\":" + std::to_string(value >> 8) + "}",
                                            "set:" + u + ":" + std::to_string(msb));
                        bridge_->push_event("{\"t\":\"set\",\"u\":" + u + ",\"c\":" + std::to_string(lsb) + ",\"v\":" + std::to_string(value & 0xFF) + "}",
                                            "set:" + u + ":" + std::to_string(lsb));
                    }
                    send_response(client_fd, 200, "OK", "OK");
                }
            }
        } else if (req.method == "GET" && req.path == "/api/state") {
            if (!bridge_) {
                send_response(client_fd, 503, "Service Unavailable", "ERR bridge not available");
            } else {
                const auto number = [&](const char* key) -> uint64_t {
                    auto it = query.find(key);
                    if (it == query.end()) return 0;
                    try { return std::stoull(it->second); } catch (...) { return 0; }
                };
                const auto snap = bridge_->snapshot(number("rev"), number("liverev"));

                std::ostringstream o;
                o << "{\"showRev\":" << snap.show_rev << ",\"show\":" << (snap.show.empty() ? "null" : snap.show)
                  << ",\"liveRev\":" << snap.live_rev << ",\"live\":" << (snap.live.empty() ? "null" : snap.live)
                  << ",\"values\":{";

                // Gewuenschte Universes (max. 8), Standard: 0
                std::vector<int> universes;
                {
                    std::istringstream list(query.count("universes") ? query["universes"] : "0");
                    std::string item;
                    while (std::getline(list, item, ',') && universes.size() < 8) {
                        try { universes.push_back(std::stoi(item)); } catch (...) {}
                    }
                }
                bool first = true;
                for (int u : universes) {
                    try {
                        const auto raw = engine_.universe(u).raw();
                        if (!first) o << ',';
                        first = false;
                        o << '"' << u << "\":[";
                        for (size_t i = 0; i < raw.size(); ++i) { if (i) o << ','; o << static_cast<int>(raw[i]); }
                        o << ']';
                    } catch (...) { /* unbekannte Universe auslassen */ }
                }
                o << "}}";
                send_response(client_fd, 200, "OK", o.str(), "application/json");
            }
        } else if (req.method == "POST" && req.path == "/api/action") {
            auto form = parse_form_body(req.body);
            const std::string type = form.count("type") ? form["type"] : "";
            // IDs kommen aus der Desktop-App (UUIDs). Nur harmlose Zeichen zulassen,
            // weil sie unveraendert in ein JSON-Ereignis eingesetzt werden.
            const auto safe_id = [](const std::string& id) {
                if (id.empty() || id.size() > 64) return false;
                return std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
            };
            const auto percent = [&](const char* key) -> int {
                if (!form.count(key)) return -1;
                try { return std::max(0, std::min(100, std::stoi(form[key]))); } catch (...) { return -1; }
            };
            if (!bridge_) {
                send_response(client_fd, 503, "Service Unavailable", "ERR bridge not available");
            } else if ((type == "cue" || type == "preset") && form.count("id") && safe_id(form["id"])) {
                bridge_->push_event("{\"t\":\"" + type + "\",\"id\":\"" + form["id"] + "\"}");
                send_response(client_fd, 200, "OK", "OK");
            } else if (type == "fog" && form.count("on")) {
                const bool on = form["on"] == "1";
                std::string json = std::string("{\"t\":\"fog\",\"on\":") + (on ? "true" : "false");
                if (const int flow = percent("flow"); flow >= 0) json += ",\"flow\":" + std::to_string(flow);
                if (const int heat = percent("heat"); heat >= 0) json += ",\"heat\":" + std::to_string(heat);
                bridge_->push_event(json + "}", "fog");
                send_response(client_fd, 200, "OK", "OK");
            } else if (type == "blackout" && form.count("on")) {
                const bool on = form["on"] == "1";
                // Blackout sofort in der Engine ausfuehren (sicherheitsrelevant, soll
                // auch wirken, wenn die Desktop-Oberflaeche gerade traege ist). Das
                // Aufheben braucht die gemerkten Werte der Desktop-App.
                if (on) engine_.blackout();
                bridge_->push_event(std::string("{\"t\":\"blackout\",\"on\":") + (on ? "true" : "false") + "}", "blackout");
                send_response(client_fd, 200, "OK", "OK");
            } else {
                send_response(client_fd, 400, "Bad Request", "ERR unknown or invalid action");
            }
        } else {
            send_response(client_fd, 404, "Not Found", "ERR unknown endpoint");
        }
    } catch (const std::exception& e) {
        send_response(client_fd, 400, "Bad Request", std::string("ERR ") + e.what());
    }

    LUMINARY_CLOSESOCK(client_fd);
    remove_client_fd(client_fd);
}
