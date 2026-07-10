#ifdef _WIN32

#include "capture_dxgi.hpp"

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

} // namespace

struct DxgiCapture::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    DXGI_OUTPUT_DESC output_desc{};

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
        hr = adapter->EnumOutputs(0, &output); // glowny monitor
        if (FAILED(hr))
            fail("EnumOutputs(0)", hr);
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

DxgiCapture::DxgiCapture() : impl_(std::make_unique<Impl>()) { impl_->init(); }

DxgiCapture::~DxgiCapture() = default;

cv::Mat DxgiCapture::grab() { return impl_->acquire_frame(); }

} // namespace predeye

#endif // _WIN32
