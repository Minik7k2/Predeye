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

    // Surowe bajty (ikony webp) — bez cache w kliencie; cache'uje warstwa wyzej.
    std::vector<unsigned char> get_binary(const std::string& path);

  private:
    std::string fetch(const std::string& url); // zywy GET z pauza >= 0,3 s
    void throttle();

    std::string cache_dir_;
    long default_ttl_s_;
    std::chrono::steady_clock::time_point last_request_{};
};

} // namespace predeye
