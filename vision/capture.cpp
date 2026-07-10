#include "capture.hpp"

#include <opencv2/imgcodecs.hpp>
#include <stdexcept>

namespace predeye {

cv::Mat FileCapture::grab() {
    cv::Mat img = cv::imread(path_, cv::IMREAD_COLOR);
    if (img.empty())
        throw std::runtime_error("nie wczytano obrazu: " + path_);
    return img;
}

} // namespace predeye
