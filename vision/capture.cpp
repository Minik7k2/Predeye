#include "capture.hpp"

#include <ctime>
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

std::string save_capture_png(const cv::Mat& frame_bgr, const std::string& dir) {
    try {
        if (frame_bgr.empty())
            return {};
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        if (const std::tm* lt = std::localtime(&now))
            tm = *lt;
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
        // Licznik rozroznia kilka odczytow w tej samej sekundzie.
        static int seq = 0;
        const std::string path =
            dir + "/live_" + stamp + "_" + std::to_string(seq++ % 1000) + ".png";
        if (!cv::imwrite(path, frame_bgr))
            return {};
        return path;
    } catch (...) {
        return {};
    }
}

} // namespace predeye
