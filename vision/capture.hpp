// vision/capture — interfejs zrodla klatek; reszta systemu zna tylko ICapture.
#pragma once

#include <opencv2/core.hpp>
#include <string>

namespace predeye {

class ICapture {
  public:
    virtual ~ICapture() = default;
    // Zwraca klatke BGR; pusta cv::Mat lub wyjatek przy bledzie zrodla.
    virtual cv::Mat grab() = 0;
};

// Testowalnosc bez gry: "capture" czytajacy obraz z pliku (PNG itd.).
class FileCapture : public ICapture {
  public:
    explicit FileCapture(std::string path) : path_(std::move(path)) {}
    cv::Mat grab() override;

  private:
    std::string path_;
};

} // namespace predeye
