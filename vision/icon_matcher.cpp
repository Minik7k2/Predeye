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

fs::path manifest_path(const std::string& dir) { return fs::path(dir) / "manifest.json"; }

std::map<long long, std::string> load_manifest(const std::string& dir) {
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

void save_manifest(const std::string& dir, const std::map<long long, std::string>& m) {
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [id, file] : m)
        j[std::to_string(id)] = file;
    std::ofstream out(manifest_path(dir), std::ios::binary | std::ios::trunc);
    out << j.dump(1);
}

} // namespace

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

std::map<long long, std::string> ensure_icon_cache(const std::vector<Item>& items,
                                                   OmedaClient& api,
                                                   const std::string& icon_cache_dir) {
    std::error_code ec;
    fs::create_directories(icon_cache_dir, ec);
    auto manifest = load_manifest(icon_cache_dir);

    int downloaded = 0, skipped = 0;
    for (const auto& it : items) {
        if (!it.buyable())
            continue;
        auto entry = manifest.find(it.id);
        if (entry != manifest.end() && fs::exists(fs::path(icon_cache_dir) / entry->second, ec))
            continue;
        if (it.image.empty()) {
            std::fprintf(stderr, "predeye: brak sciezki ikony dla %s (id %lld) — pomijam\n",
                         it.display_name.c_str(), it.id);
            ++skipped;
            continue;
        }
        try {
            const auto bytes = api.get_binary(it.image);
            const std::string file = std::to_string(it.id) + ".webp";
            std::ofstream out(fs::path(icon_cache_dir) / file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
            manifest[it.id] = file;
            ++downloaded; // pauze ~50 ms wymusza get_binary (§6.6)
        } catch (const OmedaError& ex) {
            std::fprintf(stderr, "predeye: nie pobrano ikony %s: %s — pomijam\n",
                         it.display_name.c_str(), ex.what());
            ++skipped;
        }
    }
    save_manifest(icon_cache_dir, manifest);
    if (downloaded || skipped)
        std::fprintf(stderr, "predeye: ikony — pobrano %d, pominieto %d, w bazie %d\n",
                     downloaded, skipped, static_cast<int>(manifest.size()));
    return manifest;
}

void IconMatcher::build_base(const std::map<long long, std::string>& manifest,
                             const std::string& icon_cache_dir) {
    std::vector<cv::Mat> sigs;
    sigs.reserve(manifest.size());
    for (const auto& [id, file] : manifest) {
        const cv::Mat icon = cv::imread((fs::path(icon_cache_dir) / file).string(),
                                        cv::IMREAD_COLOR);
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
    build_base(load_manifest(icon_cache_dir), icon_cache_dir);
}

MatchResult IconMatcher::match(const cv::Mat& slot_bgr) const {
    MatchResult res;
    if (signatures_.rows == 0)
        return res;

    // Etap 1: czyste NCC (WIAZACE §6.6) do calej bazy — wylania kandydatow.
    const cv::Mat sig0 = ncc_signature(slot_bgr);
    const cv::Mat scores = signatures_ * sig0.t(); // N x 1

    constexpr int kCandidates = 8;
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
            cv::warpAffine(slot_bgr, probe, shift, slot_bgr.size(), cv::INTER_NEAREST,
                           cv::BORDER_REPLICATE);
            const cv::Mat sig = ncc_signature(probe);
            for (int k = 0; k < keep; ++k) {
                const float c = static_cast<float>(
                    signatures_.row(order[static_cast<size_t>(k)]).dot(sig));
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
        res.top3[k] = {ids_[static_cast<size_t>(idx)], best[static_cast<size_t>(rerank[k])]};
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
