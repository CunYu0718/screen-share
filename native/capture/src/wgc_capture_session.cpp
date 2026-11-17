#include "screen_share/capture/wgc_capture_session.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <windows.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/base.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

namespace screen_share::capture {
namespace {

using Microsoft::WRL::ComPtr;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;
using winrt::Windows::Graphics::DirectX::DirectXPixelFormat;

inline constexpr int frame_pool_buffer_count = 2;
inline constexpr auto capture_pixel_format = DirectXPixelFormat::B8G8R8A8UIntNormalized;

struct D3DDevicePair {
    ComPtr<ID3D11Device> native_device;
    IDirect3DDevice runtime_device{nullptr};
};

[[nodiscard]] D3DDevicePair create_d3d_device() {
    D3DDevicePair devices;
    winrt::check_hresult(D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        devices.native_device.GetAddressOf(),
        nullptr,
        nullptr));

    ComPtr<IDXGIDevice> dxgi_device;
    winrt::check_hresult(devices.native_device.As(&dxgi_device));

    winrt::com_ptr<IInspectable> inspectable_device;
    winrt::check_hresult(
        CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(), inspectable_device.put()));
    devices.runtime_device = inspectable_device.as<IDirect3DDevice>();
    return devices;
}

[[nodiscard]] GraphicsCaptureItem create_capture_item(const CaptureSourceId source) {
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};

    if (source.kind == CaptureSourceKind::Monitor) {
        winrt::check_hresult(interop->CreateForMonitor(
            reinterpret_cast<HMONITOR>(source.native_handle),
            winrt::guid_of<GraphicsCaptureItem>(),
            winrt::put_abi(item)));
    } else {
        winrt::check_hresult(interop->CreateForWindow(
            reinterpret_cast<HWND>(source.native_handle),
            winrt::guid_of<GraphicsCaptureItem>(),
            winrt::put_abi(item)));
    }

    return item;
}

[[nodiscard]] winrt::com_ptr<ID3D11Texture2D> capture_texture(
    const winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface& surface) {
    const auto access =
        surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(
        winrt::guid_of<ID3D11Texture2D>(), texture.put_void()));
    return texture;
}

}  // namespace

struct WgcCaptureSession::Impl final : std::enable_shared_from_this<WgcCaptureSession::Impl> {
    ComPtr<ID3D11Device> native_device;
    IDirect3DDevice runtime_device{nullptr};
    GraphicsCaptureItem capture_item{nullptr};
    Direct3D11CaptureFramePool frame_pool{nullptr};
    GraphicsCaptureSession capture_session{nullptr};
    winrt::event_token frame_arrived_token{};
    winrt::event_token item_closed_token{};
    bool frame_arrived_subscribed{false};
    bool item_closed_subscribed{false};
    std::atomic_bool is_running{false};
    std::atomic<std::int32_t> frame_width{0};
    std::atomic<std::int32_t> frame_height{0};
    std::mutex resource_mutex;
    std::mutex handler_mutex;
    D3D11CaptureFrameHandler frame_handler;

    ~Impl() {
        stop();
    }

    [[nodiscard]] WgcCaptureStartResult start(
        const CaptureSourceId source, D3D11CaptureFrameHandler next_frame_handler) {
        if (is_running.load(std::memory_order_acquire)) {
            return WgcCaptureStartResult::AlreadyRunning;
        }
        if (source.native_handle == 0U || !next_frame_handler) {
            return WgcCaptureStartResult::InvalidSource;
        }
        if (!WgcCaptureSession::is_supported()) {
            return WgcCaptureStartResult::Unsupported;
        }

        stop();
        auto failure = WgcCaptureStartResult::DeviceCreationFailed;

        try {
            auto devices = create_d3d_device();
            native_device = std::move(devices.native_device);
            runtime_device = std::move(devices.runtime_device);

            failure = WgcCaptureStartResult::CaptureItemCreationFailed;
            capture_item = create_capture_item(source);

            failure = WgcCaptureStartResult::SessionCreationFailed;
            const auto initial_size = capture_item.Size();
            frame_width.store(initial_size.Width, std::memory_order_relaxed);
            frame_height.store(initial_size.Height, std::memory_order_relaxed);
            frame_pool = Direct3D11CaptureFramePool::CreateFreeThreaded(
                runtime_device, capture_pixel_format, frame_pool_buffer_count, initial_size);
            capture_session = frame_pool.CreateCaptureSession(capture_item);

            {
                const std::scoped_lock lock(handler_mutex);
                frame_handler = std::move(next_frame_handler);
            }

            const auto weak_self = weak_from_this();
            frame_arrived_token = frame_pool.FrameArrived(
                [weak_self](const Direct3D11CaptureFramePool& sender,
                            const winrt::Windows::Foundation::IInspectable&) noexcept {
                    if (const auto self = weak_self.lock()) {
                        self->on_frame_arrived(sender);
                    }
                });
            frame_arrived_subscribed = true;

            item_closed_token = capture_item.Closed(
                [weak_self](const GraphicsCaptureItem&,
                            const winrt::Windows::Foundation::IInspectable&) noexcept {
                    if (const auto self = weak_self.lock()) {
                        self->is_running.store(false, std::memory_order_release);
                    }
                });
            item_closed_subscribed = true;

            is_running.store(true, std::memory_order_release);
            capture_session.StartCapture();
            return WgcCaptureStartResult::Started;
        } catch (...) {
            stop();
            return failure;
        }
    }

    void stop() noexcept {
        is_running.store(false, std::memory_order_release);

        try {
            if (frame_pool && frame_arrived_subscribed) {
                frame_pool.FrameArrived(frame_arrived_token);
            }
        } catch (...) {
        }
        frame_arrived_subscribed = false;

        try {
            if (capture_item && item_closed_subscribed) {
                capture_item.Closed(item_closed_token);
            }
        } catch (...) {
        }
        item_closed_subscribed = false;

        try {
            if (capture_session) {
                capture_session.Close();
            }
        } catch (...) {
        }
        try {
            if (frame_pool) {
                frame_pool.Close();
            }
        } catch (...) {
        }

        {
            const std::scoped_lock lock(handler_mutex);
            frame_handler = nullptr;
        }
        capture_session = nullptr;
        frame_pool = nullptr;
        capture_item = nullptr;
        frame_width.store(0, std::memory_order_relaxed);
        frame_height.store(0, std::memory_order_relaxed);
        {
            const std::scoped_lock lock(resource_mutex);
            runtime_device = nullptr;
            native_device.Reset();
        }
    }

    void on_frame_arrived(const Direct3D11CaptureFramePool& sender) noexcept {
        if (!is_running.load(std::memory_order_acquire)) {
            return;
        }

        try {
            auto frame = sender.TryGetNextFrame();
            if (!frame) {
                return;
            }

            const auto content_size = frame.ContentSize();
            if (content_size.Width <= 0 || content_size.Height <= 0) {
                frame.Close();
                return;
            }

            auto texture = capture_texture(frame.Surface());
            D3D11CaptureFrameHandler handler;
            {
                const std::scoped_lock lock(handler_mutex);
                handler = frame_handler;
            }

            if (handler && is_running.load(std::memory_order_acquire)) {
                const D3D11CaptureFrameView frame_view{
                    .texture = texture.get(),
                    .width = static_cast<std::uint32_t>(content_size.Width),
                    .height = static_cast<std::uint32_t>(content_size.Height),
                    .system_relative_time_100ns = frame.SystemRelativeTime().count(),
                };
                handler(frame_view);
            }

            const bool size_changed =
                content_size.Width != frame_width.load(std::memory_order_relaxed) ||
                content_size.Height != frame_height.load(std::memory_order_relaxed);
            frame.Close();

            if (size_changed && is_running.load(std::memory_order_acquire)) {
                IDirect3DDevice frame_device{nullptr};
                {
                    const std::scoped_lock lock(resource_mutex);
                    frame_device = runtime_device;
                }
                if (frame_device) {
                    sender.Recreate(
                        frame_device, capture_pixel_format, frame_pool_buffer_count, content_size);
                    frame_width.store(content_size.Width, std::memory_order_relaxed);
                    frame_height.store(content_size.Height, std::memory_order_relaxed);
                }
            }
        } catch (...) {
            // A single unavailable frame must not unwind across the WinRT event boundary.
        }
    }
};

WgcCaptureSession::WgcCaptureSession() : implementation_(std::make_shared<Impl>()) {
}

WgcCaptureSession::~WgcCaptureSession() {
    stop();
}

bool WgcCaptureSession::is_supported() noexcept {
    try {
        return GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

WgcCaptureStartResult WgcCaptureSession::start(
    const CaptureSourceId source, D3D11CaptureFrameHandler frame_handler) {
    return implementation_->start(source, std::move(frame_handler));
}

void WgcCaptureSession::stop() noexcept {
    if (implementation_) {
        implementation_->stop();
    }
}

bool WgcCaptureSession::running() const noexcept {
    return implementation_ && implementation_->is_running.load(std::memory_order_acquire);
}

}  // namespace screen_share::capture
