// vision/hero_matcher — identyfikacja bohatera z portretu przy karcie gracza
// na scoreboardzie, ta sama metoda co itemy (kolor-NCC 32x32, §6.6).
// Grafiki bohaterow z API (/heroes.json, pole image) to TEN SAM art co
// portrety renderowane na scoreboardzie — zweryfikowane na realnych zrzutach
// 1080p. Zero OCR.
#pragma once

#include "core/hero_context.hpp"
#include "core/omeda_client.hpp"
#include "vision/icon_matcher.hpp"

#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace predeye {

class HeroMatcher {
  public:
    // Buduje baze sygnatur portretow (pobiera brakujace grafiki przez api).
    HeroMatcher(const std::vector<HeroProfile>& heroes, OmedaClient& api,
                const std::string& portrait_cache_dir);
    // Wariant offline: baza wprost z manifestu w katalogu (testy).
    explicit HeroMatcher(const std::string& portrait_cache_dir);

    // item_id wyniku = id bohatera; top3/cosine jak u itemow.
    MatchResult match(const cv::Mat& portrait_bgr) const;
    size_t base_size() const { return ids_.size(); }

  private:
    void build_base(const std::map<long long, std::string>& manifest, const std::string& dir);

    cv::Mat signatures_; // N x 3072, CV_32F — wiersz = sygnatura bohatera
    std::vector<long long> ids_;
};

} // namespace predeye
