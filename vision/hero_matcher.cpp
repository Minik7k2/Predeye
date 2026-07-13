#include "hero_matcher.hpp"

#include <cstdio>
#include <filesystem>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace fs = std::filesystem;

namespace predeye {

void HeroMatcher::build_base(const std::map<long long, std::string>& manifest,
                             const std::string& dir) {
    std::vector<cv::Mat> sigs;
    sigs.reserve(manifest.size());
    for (const auto& [id, file] : manifest) {
        cv::Mat img = cv::imread((fs::path(dir) / file).string(), cv::IMREAD_UNCHANGED);
        if (!img.empty() && img.channels() == 1)
            cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);
        // Portrety sa nieprzezroczyste — tile_from_asset to dla nich no-op,
        // ale zabezpiecza przed ewentualna alfa w przyszlych grafikach.
        img = tile_from_asset(img);
        if (img.empty()) {
            std::fprintf(stderr, "predeye: nie zdekodowano portretu %s — pomijam\n",
                         file.c_str());
            continue;
        }
        sigs.push_back(ncc_signature(img));
        ids_.push_back(id);
    }
    signatures_.create(static_cast<int>(sigs.size()), sigs.empty() ? 3072 : sigs[0].cols,
                       CV_32F);
    for (size_t i = 0; i < sigs.size(); ++i)
        sigs[i].copyTo(signatures_.row(static_cast<int>(i)));
}

HeroMatcher::HeroMatcher(const std::vector<HeroProfile>& heroes, OmedaClient& api,
                         const std::string& portrait_cache_dir) {
    std::vector<ImageRef> wanted;
    wanted.reserve(heroes.size());
    for (const auto& h : heroes)
        wanted.push_back({h.id, h.image, h.name});
    build_base(ensure_image_cache(wanted, api, portrait_cache_dir), portrait_cache_dir);
}

HeroMatcher::HeroMatcher(const std::string& portrait_cache_dir) {
    build_base(load_image_manifest(portrait_cache_dir), portrait_cache_dir);
}

MatchResult HeroMatcher::match(const cv::Mat& portrait_bgr) const {
    return match_signatures(signatures_, ids_, portrait_bgr);
}

} // namespace predeye
