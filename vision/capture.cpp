#include "capture.hpp"

#include <filesystem>
#include <fstream>
#include <opencv2/imgcodecs.hpp>
#include <stdexcept>
#include <vector>

namespace predeye {

cv::Mat FileCapture::grab() {
    // cv::imread na Windows otwiera plik waska sciezka (ANSI) — sciezki
    // z polskimi znakami zawodza. Czytamy bajty przez filesystem::path
    // (traktujac path_ jako UTF-8) i dekodujemy obraz w pamieci.
    const std::filesystem::path p = std::filesystem::u8path(path_);
    std::ifstream in(p, std::ios::binary);
    if (!in)
        throw std::runtime_error("nie mozna otworzyc pliku: " + path_);
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in),
                                           std::istreambuf_iterator<char>()};
    cv::Mat img = cv::imdecode(bytes, cv::IMREAD_COLOR);
    if (img.empty())
        throw std::runtime_error("nie wczytano obrazu (uszkodzony/nieobslugiwany format): " +
                                 path_);
    return img;
}

} // namespace predeye
