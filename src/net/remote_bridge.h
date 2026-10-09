#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace luminary::net {

    // Briefkasten zwischen Desktop-App (Unix-Socket) und Mobile-PWA (HTTP).
    //
    // Die Engine kennt weder Cues noch Presets noch Fixture-Namen - das ist
    // alles Zustand der Desktop-App. Damit die PWA ihn trotzdem sehen und
    // bedienen kann, haelt die Engine nur zwei undurchsichtige JSON-Texte
    // und eine Ereignis-Warteschlange vor:
    //
    //   show  statische Daten (Patches, Gruppen, Cue-/Preset-Namen, Nebel-Setup)
    //         Desktop -> SET_SHOW -> PWA liest per GET /api/state
    //   live  fluechtiger Zustand (Blackout, Nebel an/aus, ...)
    //         Desktop -> SET_LIVE -> PWA liest per GET /api/state
    //   events  Handy-Aktionen (Fader, Cue abrufen, ...)
    //         PWA -> HTTP -> Warteschlange -> EVENTS -> Desktop fuehrt sie aus
    //
    // Die Engine validiert den Inhalt nicht inhaltlich, prueft aber Groesse
    // und dass es ein einzeiliges JSON-Objekt ist.
    class RemoteBridge {
    public:
        static constexpr size_t MAX_BLOB_BYTES = 1 * 1024 * 1024;
        static constexpr size_t MAX_EVENTS = 512;

        // false, wenn nicht als einzeiliges JSON-Objekt akzeptiert.
        bool set_show(std::string json);
        bool set_live(std::string json);

        struct Snapshot {
            uint64_t show_rev = 0;
            std::string show;     // leer, wenn der Client die Revision schon kennt
            uint64_t live_rev = 0;
            std::string live;     // leer, wenn der Client die Revision schon kennt
        };
        Snapshot snapshot(uint64_t known_show_rev, uint64_t known_live_rev) const;

        // coalesce_key: Ereignisse mit gleichem nicht-leeren Key ersetzen sich
        // (z.B. Fader-Zieherei) statt die Warteschlange zu fluten.
        void push_event(std::string json, const std::string& coalesce_key = "");
        std::vector<std::string> drain_events();

        static bool is_single_line_object(const std::string& s);

    private:
        mutable std::mutex mutex_;
        std::string show_;
        std::string live_;
        uint64_t show_rev_ = 0;
        uint64_t live_rev_ = 0;
        std::deque<std::pair<std::string, std::string>> events_;   // (key, json)
    };

} // namespace luminary::net
