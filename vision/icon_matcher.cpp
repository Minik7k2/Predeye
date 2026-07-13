#include "icon_matcher.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace fs = std::filesystem;

namespace predeye {
namespace {

constexpr int kSide = 32;

// Kolor tla kafelka itemu na scoreboardzie (BGR) — zmierzony z naroznikow
// realnych slotow 1080p (patch 5.4.4). Uzywany do kompozycji alfy grafik API.
const cv::Scalar kTileBg(18.0, 21.0, 24.0);

fs::path manifest_path(const std::string& dir) { return fs::path(dir) / "manifest.json"; }

void save_manifest(const std::string& dir, const std::map<long long, std::string>& m) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [id, file] : m)
        j[std::to_string(id)] = file;
    std::ofstream out(manifest_path(dir), std::ios::binary | std::ios::trunc);
    out << j.dump(1);
}

} // namespace

// Grafiki /assets/*.webp z API to duze obrazy (~420 px) z kanalem alfa,
// biala winieta w RGB pod przezroczystoscia i marginesami — to NIE sa
// kafelki renderowane na scoreboardzie (ciemne tlo, art wypelnia slot).
// imread(IMREAD_COLOR) wtapial biel do sygnatur i psul NCC na realnych
// zrzutach (zmierzone w M4). Przygotowanie: kwadratowy bbox po alfie
// (jednorodna skala, jak kafelek w grze) + kompozycja na tle kafelka.
cv::Mat tile_from_asset(const cv::Mat& img) {
    if (img.empty() || img.channels() != 4)
        return img;
    std::vector<cv::Mat> ch;
    cv::split(img, ch);
    const cv::Rect box = cv::boundingRect(ch[3] > 32);
    if (box.width <= 0 || box.height <= 0)
        return {};
    const int side = std::max(box.width, box.height);
    const cv::Point center(box.x + box.width / 2, box.y + box.height / 2);
    const cv::Rect square(center.x - side / 2, center.y - side / 2, side, side);
    cv::Mat tile(side, side, CV_8UC3, kTileBg);
    // Kwadrat moze wystawac poza obraz zrodlowy — tam zostaje samo tlo.
    const cv::Rect src = square & cv::Rect(0, 0, img.cols, img.rows);
    for (int y = 0; y < src.height; ++y) {
        const cv::Vec4b* in = img.ptr<cv::Vec4b>(src.y + y) + src.x;
        cv::Vec3b* out = tile.ptr<cv::Vec3b>(src.y - square.y + y) + (src.x - square.x);
        for (int x = 0; x < src.width; ++x) {
            const float a = static_cast<float>(in[x][3]) / 255.0f;
            for (int c = 0; c < 3; ++c)
                out[x][c] = cv::saturate_cast<uchar>(
                    a * static_cast<float>(in[x][c]) +
                    (1.0f - a) * static_cast<float>(kTileBg[c]));
        }
    }
    return tile;
}

cv::Mat ncc_signature(const cv::Mat& bgr) {
    CV_Assert(!bgr.empty() && bgr.type() == CV_8UC3);
    cv::Mat small;
    cv::resize(bgr, small, cv::Size(kSide, kSide), 0, 0, cv::INTER_AREA);
    // Delikatny low-pass po obu stronach (baza i probka): NCC na ostrych
    // 32x32 jest wrazliwe na 1-2 px jittera ROI; blur zmierzony w harnessie
    // podnosi rezim pesymistyczny ponad wiazacy prog bez zmiany metody.
    cv::GaussianBlur(small, small, cv::Size(0, 0), 1.2);
    cv::Mat f;
    small.convertTo(f, CV_32F);
    f = f.reshape(1, 1); // 1 x 3072
    f -= cv::mean(f)[0];
    const double n = cv::norm(f);
    if (n > 1e-9)
        f /= n;
    return f;
}

std::map<long long, std::string> load_image_manifest(const std::string& dir) {
    std::map<long long, std::string> out;
    std::ifstream in(manifest_path(dir), std::ios::binary);
    if (!in)
        return out;
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return out;
    for (const auto& [k, v] : j.items())
        if (v.is_string())
            out[std::stoll(k)] = v.get<std::string>();
    return out;
}

std::map<long long, std::string> ensure_image_cache(const std::vector<ImageRef>& wanted,
                                                    OmedaClient& api, const std::string& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    auto manifest = load_image_manifest(dir);

    int downloaded = 0, skipped = 0;
    for (const auto& ref : wanted) {
        auto entry = manifest.find(ref.id);
        if (entry != manifest.end() && fs::exists(fs::path(dir) / entry->second, ec))
            continue;
        if (ref.image.empty()) {
            std::fprintf(stderr, "predeye: brak sciezki grafiki dla %s (id %lld) — pomijam\n",
                         ref.label.c_str(), ref.id);
            ++skipped;
            continue;
        }
        try {
            const auto bytes = api.get_binary(ref.image);
            const std::string file = std::to_string(ref.id) + ".webp";
            std::ofstream out(fs::path(dir) / file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            manifest[ref.id] = file;
            ++downloaded; // pauze ~50 ms wymusza get_binary (§6.6)
        } catch (const OmedaError& ex) {
            std::fprintf(stderr, "predeye: nie pobrano grafiki %s: %s — pomijam\n",
                         ref.label.c_str(), ex.what());
            ++skipped;
        }
    }
    save_manifest(dir, manifest);
    if (downloaded || skipped)
        std::fprintf(stderr, "predeye: grafiki — pobrano %d, pominieto %d, w bazie %d\n",
                     downloaded, skipped, static_cast<int>(manifest.size()));
    return manifest;
}

std::map<long long, std::string> ensure_icon_cache(const std::vector<Item>& items,
                                                   OmedaClient& api,
                                                   const std::string& icon_cache_dir) {
    std::vector<ImageRef> wanted;
    for (const auto& it : items)
        if (it.buyable())
            wanted.push_back({it.id, it.image, it.display_name});
    return ensure_image_cache(wanted, api, icon_cache_dir);
}

void IconMatcher::build_base(const std::map<long long, std::string>& manifest,
                             const std::string& icon_cache_dir) {
    std::vector<cv::Mat> sigs;
    sigs.reserve(manifest.size());
    for (const auto& [id, file] : manifest) {
        cv::Mat icon = cv::imread((fs::path(icon_cache_dir) / file).string(),
                                  cv::IMREAD_UNCHANGED);
        if (!icon.empty() && icon.channels() == 1)
            cv::cvtColor(icon, icon, cv::COLOR_GRAY2BGR);
        icon = tile_from_asset(icon);
        if (icon.empty()) {
            std::fprintf(stderr, "predeye: nie zdekodowano ikony %s — pomijam\n", file.c_str());
            continue;
        }
        sigs.push_back(ncc_signature(icon));
        ids_.push_back(id);
    }
    signatures_.create(static_cast<int>(sigs.size()), kSide * kSide * 3, CV_32F);
    for (size_t i = 0; i < sigs.size(); ++i)
        sigs[i].copyTo(signatures_.row(static_cast<int>(i)));
}

IconMatcher::IconMatcher(const std::vector<Item>& items, OmedaClient& api,
                         const std::string& icon_cache_dir) {
    build_base(ensure_icon_cache(items, api, icon_cache_dir), icon_cache_dir);
}

IconMatcher::IconMatcher(const std::string& icon_cache_dir) {
    build_base(load_image_manifest(icon_cache_dir), icon_cache_dir);
}

MatchResult IconMatcher::match(const cv::Mat& slot_bgr) const {
    return match_signatures(signatures_, ids_, slot_bgr);
}

MatchResult match_signatures(const cv::Mat& signatures, const std::vector<long long>& ids,
                             const cv::Mat& probe_bgr) {
    MatchResult res;
    if (signatures.rows == 0)
        return res;

    // Etap 1: czyste NCC (WIAZACE §6.6) do calej bazy — wylania kandydatow.
    const cv::Mat sig0 = ncc_signature(probe_bgr);
    const cv::Mat scores = signatures * sig0.t(); // N x 1

    // 16 (nie 8): przy 213 ikonach rodziny blizniaczych grafik (sztylety,
    // toniki) potrafia zepchnac wlasciwy item za czolowke etapu 1;
    // zmierzone harnessem — 8 gubilo Sai/Spell Slasher w top-3 pesymistycznie.
    constexpr int kCandidates = 16;
    std::vector<int> order(static_cast<size_t>(scores.rows));
    for (int i = 0; i < scores.rows; ++i)
        order[static_cast<size_t>(i)] = i;
    const int keep = std::min(kCandidates, scores.rows);
    std::partial_sort(order.begin(), order.begin() + keep, order.end(), [&](int a, int b) {
        return scores.at<float>(a, 0) > scores.at<float>(b, 0);
    });

    // Etap 2: kalibracja ROI bywa przesunieta o 1-2 px — dla samych kandydatow
    // szukamy najlepszego wyrownania probki (max cosine po przesunieciach).
    // Ograniczenie do top-K unika inflacji trafien dalekich itemow, ktora
    // psula ranking przy maksimum po calej bazie (zmierzone w harnessie).
    std::vector<float> best(static_cast<size_t>(keep));
    for (int k = 0; k < keep; ++k)
        best[static_cast<size_t>(k)] = scores.at<float>(order[static_cast<size_t>(k)], 0);
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0)
                continue;
            const cv::Mat shift = (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy);
            cv::Mat probe;
            cv::warpAffine(probe_bgr, probe, shift, probe_bgr.size(), cv::INTER_NEAREST,
                           cv::BORDER_REPLICATE);
            const cv::Mat sig = ncc_signature(probe);
            for (int k = 0; k < keep; ++k) {
                const float c = static_cast<float>(
                    signatures.row(order[static_cast<size_t>(k)]).dot(sig));
                best[static_cast<size_t>(k)] = std::max(best[static_cast<size_t>(k)], c);
            }
        }
    }

    std::vector<int> rerank(static_cast<size_t>(keep));
    for (int k = 0; k < keep; ++k)
        rerank[static_cast<size_t>(k)] = k;
    std::stable_sort(rerank.begin(), rerank.end(),
                     [&](int a, int b) { return best[static_cast<size_t>(a)] > best[static_cast<size_t>(b)]; });

    for (size_t k = 0; k < 3 && k < static_cast<size_t>(keep); ++k) {
        const int idx = order[static_cast<size_t>(rerank[k])];
        res.top3[k] = {ids[static_cast<size_t>(idx)], best[static_cast<size_t>(rerank[k])]};
    }
    res.item_id = res.top3[0].first;
    res.cosine = res.top3[0].second;
    return res;
}

bool IconMatcher::looks_empty(const cv::Mat& slot_bgr) {
    // Puste sloty sa ciemne i jednolite. Progi WSTEPNE — strojenie w M4.
    cv::Mat gray;
    cv::cvtColor(slot_bgr, gray, cv::COLOR_BGR2GRAY);
    cv::Scalar mean, stddev;
    cv::meanStdDev(gray, mean, stddev);
    return mean[0] < 40.0 && stddev[0] < 18.0;
}

} // namespace predeye
