#ifdef _WIN32

#include "capture_dxgi.hpp"

#include <cwchar>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <opencv2/imgproc.hpp>
#include <stdexcept>
#include <string>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace predeye {
namespace {

[[noreturn]] void fail(const char* what, HRESULT hr) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "%s (HRESULT 0x%08lX)", what, static_cast<unsigned long>(hr));
    throw std::runtime_error(buf);
}

// Monitor z widocznym oknem gry. Filtr po klasie okna UE ("UnrealWindow")
// ORAZ prefiksie tytulu — sam tytul zlapalby np. karte przegladarki
// "Predecessor Wiki". Publiczne API okien, zero ingerencji w gre.
HMONITOR find_game_monitor() {
    HMONITOR found = nullptr;
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            if (!IsWindowVisible(hwnd))
                return TRUE;
            wchar_t cls[64] = {};
            GetClassNameW(hwnd, cls, 63);
            if (std::wcscmp(cls, L"UnrealWindow") != 0)
                return TRUE;
            wchar_t title[128] = {};
            GetWindowTextW(hwnd, title, 127);
            if (std::wcsncmp(title, L"Predecessor", 11) != 0)
                return TRUE;
            *reinterpret_cast<HMONITOR*>(lp) = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&found));
    return found;
}

} // namespace

struct DxgiCapture::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    DXGI_OUTPUT_DESC output_desc{};
    int requested_output = -1;

    void init() {
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr,
                                       0, D3D11_SDK_VERSION, &device, nullptr, &context);
        if (FAILED(hr))
            fail("D3D11CreateDevice", hr);

        ComPtr<IDXGIDevice> dxgi_device;
        hr = device.As(&dxgi_device);
        if (FAILED(hr))
            fail("IDXGIDevice", hr);
        ComPtr<IDXGIAdapter> adapter;
        hr = dxgi_device->GetAdapter(&adapter);
        if (FAILED(hr))
            fail("GetAdapter", hr);
        ComPtr<IDXGIOutput> output;
        if (requested_output >= 0) {
            hr = adapter->EnumOutputs(static_cast<UINT>(requested_output), &output);
            if (FAILED(hr))
                fail("EnumOutputs(zadany monitor — sprawdz --monitor)", hr);
        } else {
            // Auto: wyjscie z oknem gry; bez okna zostaje glowny monitor (0).
            const HMONITOR game = find_game_monitor();
            ComPtr<IDXGIOutput> cand;
            for (UINT i = 0; SUCCEEDED(adapter->EnumOutputs(i, &cand)); ++i, cand.Reset()) {
                DXGI_OUTPUT_DESC d{};
                cand->GetDesc(&d);
                if (i == 0 || (game && d.Monitor == game))
                    output = cand;
                if (game && d.Monitor == game)
                    break;
            }
            if (!output)
                fail("EnumOutputs(0)", DXGI_ERROR_NOT_FOUND);
        }
        output->GetDesc(&output_desc);
        ComPtr<IDXGIOutput1> output1;
        hr = output.As(&output1);
        if (FAILED(hr))
            fail("IDXGIOutput1", hr);
        hr = output1->DuplicateOutput(device.Get(), &duplication);
        if (FAILED(hr))
            fail("DuplicateOutput (fullscreen exclusive? uzyj borderless)", hr);
    }

    cv::Mat acquire_frame() {
        // Duplication oddaje klatke tylko przy zmianie ekranu — probujemy
        // kilkukrotnie; pulpit z gra odswieza sie praktycznie zawsze.
        for (int attempt = 0; attempt < 10; ++attempt) {
            DXGI_OUTDUPL_FRAME_INFO info{};
            ComPtr<IDXGIResource> resource;
            HRESULT hr = duplication->AcquireNextFrame(500, &info, &resource);
            if (hr == DXGI_ERROR_WAIT_TIMEOUT)
                continue;
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                // Zmiana trybu/monitora — odtworz duplikacje i probuj dalej.
                duplication.Reset();
                context.Reset();
                device.Reset();
                init();
                continue;
            }
            if (FAILED(hr))
                fail("AcquireNextFrame", hr);

            ComPtr<ID3D11Texture2D> gpu_tex;
            hr = resource.As(&gpu_tex);
            if (FAILED(hr)) {
                duplication->ReleaseFrame();
                fail("ID3D11Texture2D", hr);
            }

            D3D11_TEXTURE2D_DESC desc{};
            gpu_tex->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.BindFlags = 0;
            desc.MiscFlags = 0;

            ComPtr<ID3D11Texture2D> cpu_tex;
            hr = device->CreateTexture2D(&desc, nullptr, &cpu_tex);
            if (FAILED(hr)) {
                duplication->ReleaseFrame();
                fail("CreateTexture2D(staging)", hr);
            }
            context->CopyResource(cpu_tex.Get(), gpu_tex.Get());

            D3D11_MAPPED_SUBRESOURCE mapped{};
            hr = context->Map(cpu_tex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) {
                duplication->ReleaseFrame();
                fail("Map(staging)", hr);
            }

            // BGRA z wlasnym stride -> gleboka kopia -> BGR.
            const cv::Mat bgra(static_cast<int>(desc.Height), static_cast<int>(desc.Width),
                               CV_8UC4, mapped.pData, mapped.RowPitch);
            cv::Mat bgr;
            cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);

            context->Unmap(cpu_tex.Get(), 0);
            duplication->ReleaseFrame();
            return bgr;
        }
        throw std::runtime_error("brak klatki z duplikacji pulpitu (timeout)");
    }
};

DxgiCapture::DxgiCapture(int output) : impl_(std::make_unique<Impl>()) {
    impl_->requested_output = output;
    impl_->init();
}

DxgiCapture::~DxgiCapture() = default;

cv::Mat DxgiCapture::grab() { return impl_->acquire_frame(); }

} // namespace predeye

#endif // _WIN32
