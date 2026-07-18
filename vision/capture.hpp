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

// Zapis klatki odczytu live do katalogu (PNG, nazwa z data i godzina).
// Kazdy odczyt z gry zostaje na dysku: bledne rozpoznanie da sie odtworzyc
// offline (`live --image`), a klatki to materiał do strojenia progow
// (looks_empty/cosine) i fixtures regresji. Nigdy nie rzuca — zwraca
// sciezke zapisanego pliku albo "" przy bledzie (zapis nie moze psuc odczytu).
std::string save_capture_png(const cv::Mat& frame_bgr, const std::string& dir = "captures");

} // namespace predeye
