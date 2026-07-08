#include "omeda_client.hpp"

#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace predeye {
namespace {

constexpr const char* kUserAgent = "predeye/0.1.0 (osobisty trener; rozsadne uzycie)";
constexpr long kTimeoutS = 30;
constexpr auto kMinRequestGap = std::chrono::milliseconds(300);

// FNV-1a 64-bit — stabilny hash sciezki na nazwe pliku cache (std::hash nie
// gwarantuje stabilnosci miedzy uruchomieniami/platformami).
std::string path_hash(const std::string& s) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", h);
    return buf;
}

size_t write_to_string(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// Jednorazowa globalna inicjalizacja libcurl (RAII przez static).
struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

std::string curl_get(const std::string& url) {
    static CurlGlobal global;
    CURL* h = curl_easy_init();
    if (!h)
        throw OmedaError("curl_easy_init nie powiodl sie");
    std::string body;
    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(h, CURLOPT_TIMEOUT, kTimeoutS);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_to_string);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, errbuf);
    CURLcode rc = curl_easy_perform(h);
    long status = 0;
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(h);
    if (rc != CURLE_OK)
        throw OmedaError("blad sieci dla " + url + ": " +
                         (errbuf[0] ? errbuf : curl_easy_strerror(rc)));
    if (status >= 400)
        throw OmedaError("HTTP " + std::to_string(status) + " dla " + url);
    return body;
}

} // namespace

std::string OmedaClient::default_dir() {
#ifdef _WIN32
    if (const char* app = std::getenv("LOCALAPPDATA"))
        return std::string(app) + "\\predeye\\cache";
    return "predeye_cache";
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"))
        return std::string(xdg) + "/predeye";
    if (const char* home = std::getenv("HOME"))
        return std::string(home) + "/.cache/predeye";
    return "predeye_cache";
#endif
}

OmedaClient::OmedaClient(std::string cache_dir, long default_ttl_s)
    : cache_dir_(std::move(cache_dir)), default_ttl_s_(default_ttl_s) {
    std::error_code ec;
    fs::create_directories(cache_dir_, ec); // brak katalogu to nie blad krytyczny
}

void OmedaClient::throttle() {
    // Pauza >= 0,3 s miedzy ZYWYMI zapytaniami — API nieoficjalne (§3.3).
    auto now = std::chrono::steady_clock::now();
    if (last_request_.time_since_epoch().count() != 0) {
        auto elapsed = now - last_request_;
        if (elapsed < kMinRequestGap)
            std::this_thread::sleep_for(kMinRequestGap - elapsed);
    }
    last_request_ = std::chrono::steady_clock::now();
}

std::string OmedaClient::fetch(const std::string& url) {
    throttle();
    return curl_get(url);
}

nlohmann::json OmedaClient::get(const std::string& path, long ttl_override) {
    const long ttl = ttl_override < 0 ? default_ttl_s_ : ttl_override;
    const fs::path cache_file = fs::path(cache_dir_) / (path_hash(path) + ".json");

    std::error_code ec;
    if (fs::exists(cache_file, ec)) {
        auto mtime = fs::last_write_time(cache_file, ec);
        if (!ec) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                fs::file_time_type::clock::now() - mtime);
            if (age.count() < ttl) {
                std::ifstream in(cache_file, std::ios::binary);
                std::stringstream ss;
                ss << in.rdbuf();
                auto parsed = nlohmann::json::parse(ss.str(), nullptr, false);
                if (!parsed.is_discarded())
                    return parsed;
                // uszkodzony cache => pobierz na nowo
            }
        }
    }

    const std::string body = fetch(std::string(kBaseUrl) + path);
    auto parsed = nlohmann::json::parse(body, nullptr, false);
    if (parsed.is_discarded())
        throw OmedaError("niepoprawny JSON z " + path);
    std::ofstream out(cache_file, std::ios::binary | std::ios::trunc);
    if (out)
        out << body;
    return parsed;
}

nlohmann::json OmedaClient::items() { return get("/items.json", 24 * 3600); }

nlohmann::json OmedaClient::heroes() { return get("/heroes.json", 24 * 3600); }

nlohmann::json OmedaClient::builds(long long hero_id, const std::string& role,
                                   const std::string& order) {
    std::string base = "/builds.json?filter[hero_id]=" + std::to_string(hero_id);
    if (!role.empty())
        base += "&filter[role]=" + role;
    base += "&filter[order]=" + order;

    auto with_cv = get(base + "&filter[current_version]=1", 3600);
    if (with_cv.is_array() && !with_cv.empty())
        return with_cv;
    // API zwraca dzis pusta liste dla current_version=1 — fallback bez filtra,
    // zeby nie zostac bez zadnych buildow (intencja: preferuj biezacy patch).
    return get(base, 3600);
}

std::vector<unsigned char> OmedaClient::get_binary(const std::string& path) {
    const std::string body = fetch(std::string(kBaseUrl) + path);
    return std::vector<unsigned char>(body.begin(), body.end());
}

} // namespace predeye
