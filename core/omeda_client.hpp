// core/omeda_client — klient HTTP API Omeda.city z cache dyskowym TTL.
#pragma once

#include <chrono>
#include <nlohmann/json_fwd.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace predeye {

// Bledy sieci / HTTP >= 400 / uszkodzonej odpowiedzi — czytelny komunikat.
class OmedaError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class OmedaClient {
  public:
    static std::string default_dir(); // %LOCALAPPDATA%/predeye/cache lub ~/.cache/predeye
    // NIE zmieniac na pred.gg: serwis przeniosl strony HTML i /assets/* na
    // pred.gg (308), ale stare JSON API (heroes/items/builds...) dziala TYLKO
    // bezposrednio na omeda.city — na pred.gg te sciezki to 404, a nowe API
    // pred.gg ma inny, niekompatybilny schemat (zmierzone 2026-07-13).
    // Redirect assetow zalatwia CURLOPT_FOLLOWLOCATION.
    static constexpr const char* kBaseUrl = "https://omeda.city";

    explicit OmedaClient(std::string cache_dir = default_dir(), long default_ttl_s = 21600);

    // GET base+path z cache TTL (sekundy); ttl_override < 0 => domyslny TTL.
    nlohmann::json get(const std::string& path, long ttl_override = -1);

    nlohmann::json items();  // TTL 24 h
    nlohmann::json heroes(); // TTL 24 h
    // Buildy spolecznosci; role "" = dowolna. TTL 1 h.
    // Uwaga: filter[current_version]=1 zwraca dzis pusta liste (API zmienilo
    // zachowanie od czasu prototypu) — przy pustym wyniku ponawiamy bez filtra.
    nlohmann::json builds(long long hero_id, const std::string& role,
                          const std::string& order = "popular");

    // Meta globalna (/dashboard/hero_statistics.json). TTL 6 h.
    // Uwaga (§5): winrate w skali 0-100; display_name bywa "Unknown" (mapowac
    // po hero_id); zawiera smieciowy wiersz-agregat (id spoza /heroes.json).
    nlohmann::json meta(const std::string& time_frame = "1M",
                        const std::string& game_mode = "ranked");

    // Surowe bajty (ikony webp) — bez cache w kliencie; cache'uje warstwa wyzej.
    // Statyczne assety maja krotsza pauze (~50 ms) niz zapytania API (§6.6).
    std::vector<unsigned char> get_binary(const std::string& path);

  private:
    // Zywy GET; min_gap_ms = minimalna pauza od poprzedniego zywego zapytania.
    std::string fetch(const std::string& url, long min_gap_ms = 300);
    void throttle(long min_gap_ms);

    std::string cache_dir_;
    long default_ttl_s_;
    std::chrono::steady_clock::time_point last_request_{};
};

} // namespace predeye
