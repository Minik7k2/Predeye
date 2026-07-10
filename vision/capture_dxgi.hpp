// vision/capture_dxgi — zrzut pulpitu przez DXGI Desktop Duplication (Windows).
// Jedyne wejscie wizualne projektu: publiczne API systemu, zero ingerencji w gre.
#pragma once
#ifdef _WIN32

#include "capture.hpp"

#include <memory>

namespace predeye {

class DxgiCapture : public ICapture {
  public:
    // Duplikuje glowny monitor (output 0 pierwszego adaptera).
    DxgiCapture();
    ~DxgiCapture() override;

    // Klatka BGR calego pulpitu. Fullscreen exclusive moze nie byc
    // duplikowalny (udokumentowane w README) — wtedy wyjatek.
    cv::Mat grab() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace predeye

#endif // _WIN32
