// vision/capture_dxgi — zrzut pulpitu przez DXGI Desktop Duplication (Windows).
// Jedyne wejscie wizualne projektu: publiczne API systemu, zero ingerencji w gre.
#pragma once
#ifdef _WIN32

#include "capture.hpp"

#include <memory>

namespace predeye {

class DxgiCapture : public ICapture {
  public:
    // output >= 0: duplikuj wskazane wyjscie pierwszego adaptera (0 = glowny
    // monitor). output < 0 (domyslnie): znajdz okno gry (klasa UnrealWindow,
    // tytul "Predecessor...") i duplikuj JEGO monitor — istotne przy kilku
    // monitorach; gdy okna nie ma, wyjscie 0. Wylacznie publiczne API okien.
    explicit DxgiCapture(int output = -1);
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
