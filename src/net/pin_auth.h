#pragma once
#include <chrono>
#include <mutex>
#include <random>
#include <string>
#include <unordered_set>

namespace luminary::net {

    // Verwaltet die PIN-Pairing-Logik fuer den isolierten Mobile-HTTP-Zugriff.
    //
    // Ablauf:
    //  1. Electron ruft ueber den lokalen (vertrauenswuerdigen) Unix-Socket
    //     generate_pin() auf und zeigt den PIN im UI an.
    //  2. Die Mobile-App liest den PIN vom Nutzer ein, schickt ihn per HTTP
    //     an /api/pair. redeem_pin() prueft ihn und gibt bei Erfolg ein
    //     Session-Token zurueck, das der PIN sofort ungueltig macht
    //     (Single-Use) - verhindert wiederholtes Ausprobieren im selben WLAN.
    //  3. Alle weiteren HTTP-Requests der Mobile-App tragen das Token im
    //     Authorization-Header, geprueft ueber is_valid_token().
    //
    // Bewusst KEIN TLS/Verschluesselung hier - wie besprochen fuer den
    // aktuellen Einsatzzweck (geschlossenes, vertrautes WLAN) nicht noetig,
    // aber klar als eigener, isolierter Codepfad gehalten, damit TLS sich
    // spaeter sauber nachruesten laesst, ohne den lokalen Unix-Socket-Pfad
    // anzufassen.
    class PinAuth {
    public:
        PinAuth();

        // Neuen PIN erzeugen, macht einen evtl. vorher aktiven PIN ungueltig.
        // digits: Laenge des PIN (Default 6, wie beim WLAN-Pairing ueblich).
        std::string generate_pin(int digits = 6,
                                  std::chrono::seconds validity = std::chrono::minutes(5));

        // Versucht, den PIN gegen ein Session-Token einzutauschen.
        // Leerer String = ungueltig, abgelaufen, oder bereits verbraucht.
        std::string redeem_pin(const std::string& pin);

        bool is_valid_token(const std::string& token) const;
        void revoke_token(const std::string& token);
        void revoke_all_tokens();

    private:
        mutable std::mutex mutex_;
        std::mt19937 rng_;

        std::string current_pin_;
        std::chrono::steady_clock::time_point pin_expiry_;
        bool pin_consumed_ = true;

        std::unordered_set<std::string> valid_tokens_;

        std::string generate_random_hex_locked(size_t bytes);
    };

} // namespace luminary::net
