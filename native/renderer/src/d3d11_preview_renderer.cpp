#include "screen_share/renderer/d3d11_preview_renderer.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>

namespace screen_share::renderer {
namespace {

using Microsoft::WRL::ComPtr;

inline constexpr DXGI_FORMAT preview_format = DXGI_FORMAT_B8G8R8A8_UNORM;
inline constexpr UINT preview_buffer_count = 2;

}  // namespace

struct D3D11PreviewRenderer::Impl final {
    explicit Impl(const std::uintptr_t target_handle)
        : target_window(reinterpret_cast<HWND>(target_handle)) {
    }

    HWND target_window;
    std::mutex mutex;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDXGISwapChain1> swap_chain;
    std::uint32_t buffer_width{0};
    std::uint32_t buffer_height{0};

    void release_device_resources() noexcept {
        swap_chain.Reset();
        factory.Reset();
        context.Reset();
        device.Reset();
        buffer_width = 0;
        buffer_height = 0;
    }

    [[nodiscard]] bool bind_device(ComPtr<ID3D11Device> next_device) {
        if (!next_device) {
            return false;
        }
        if (device.Get() == next_device.Get()) {
            return true;
        }

        release_device_resources();
        device = std::move(next_device);
        device->GetImmediateContext(context.GetAddressOf());
        if (!context) {
            release_device_resources();
            return false;
        }

        ComPtr<IDXGIDevice> dxgi_device;
        if (FAILED(device.As(&dxgi_device))) {
            release_device_resources();
            return false;
        }

        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgi_device->GetAdapter(adapter.GetAddressOf())) ||
            FAILED(adapter->GetParent(
                __uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf())))) {
            release_device_resources();
            return false;
        }

        return true;
    }

    [[nodiscard]] bool ensure_device(ID3D11Texture2D* const texture) {
        ComPtr<ID3D11Device> frame_device;
        texture->GetDevice(frame_device.GetAddressOf());
        return bind_device(std::move(frame_device));
    }

    [[nodiscard]] bool ensure_upload_device() {
        if (device) {
            return true;
        }

        ComPtr<ID3D11Device> upload_device;
        const auto create_result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            upload_device.GetAddressOf(),
            nullptr,
            nullptr);
        return SUCCEEDED(create_result) && bind_device(std::move(upload_device));
    }

    [[nodiscard]] bool ensure_swap_chain(const std::uint32_t width,
                                         const std::uint32_t height) {
        if (swap_chain && width == buffer_width && height == buffer_height) {
            return true;
        }
        if (width == 0U || height == 0U || target_window == nullptr ||
            IsWindow(target_window) == FALSE) {
            return false;
        }

        if (swap_chain) {
            const auto resize_result =
                swap_chain->ResizeBuffers(preview_buffer_count, width, height, preview_format, 0);
            if (FAILED(resize_result)) {
                swap_chain.Reset();
                buffer_width = 0;
                buffer_height = 0;
                return false;
            }
        } else {
            DXGI_SWAP_CHAIN_DESC1 description{};
            description.Width = width;
            description.Height = height;
            description.Format = preview_format;
            description.SampleDesc.Count = 1;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount = preview_buffer_count;
            description.Scaling = DXGI_SCALING_STRETCH;
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

            const auto create_result = factory->CreateSwapChainForHwnd(
                device.Get(), target_window, &description, nullptr, nullptr,
                swap_chain.GetAddressOf());
            if (FAILED(create_result)) {
                swap_chain.Reset();
                return false;
            }
            static_cast<void>(
                factory->MakeWindowAssociation(target_window, DXGI_MWA_NO_ALT_ENTER));
        }

        buffer_width = width;
        buffer_height = height;
        return true;
    }

    [[nodiscard]] PreviewRenderResult render(
        const screen_share::capture::D3D11CaptureFrameView& frame) {
        if (frame.texture == nullptr || frame.width == 0U || frame.height == 0U) {
            return PreviewRenderResult::InvalidFrame;
        }
        if (target_window == nullptr || IsWindow(target_window) == FALSE) {
            return PreviewRenderResult::TargetUnavailable;
        }
        if (!ensure_device(frame.texture) || !ensure_swap_chain(frame.width, frame.height)) {
            return PreviewRenderResult::Failed;
        }

        D3D11_TEXTURE2D_DESC source_description{};
        frame.texture->GetDesc(&source_description);
        if (source_description.Format != preview_format) {
            return PreviewRenderResult::InvalidFrame;
        }

        ComPtr<ID3D11Texture2D> back_buffer;
        const auto buffer_result = swap_chain->GetBuffer(
            0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back_buffer.GetAddressOf()));
        if (FAILED(buffer_result)) {
            return PreviewRenderResult::Failed;
        }

        ComPtr<ID3D11RenderTargetView> render_target;
        if (FAILED(device->CreateRenderTargetView(
                back_buffer.Get(), nullptr, render_target.GetAddressOf()))) {
            return PreviewRenderResult::Failed;
        }
        constexpr float black[]{0.0F, 0.0F, 0.0F, 1.0F};
        context->ClearRenderTargetView(render_target.Get(), black);

        const auto copy_width = std::min(frame.width, source_description.Width);
        const auto copy_height = std::min(frame.height, source_description.Height);
        const D3D11_BOX source_box{
            .left = 0,
            .top = 0,
            .front = 0,
            .right = copy_width,
            .bottom = copy_height,
            .back = 1,
        };
        context->CopySubresourceRegion(
            back_buffer.Get(), 0, 0, 0, 0, frame.texture, 0, &source_box);

        return present();
    }

    [[nodiscard]] PreviewRenderResult render(const BgraFrameView& frame) {
        if (frame.data == nullptr || frame.width == 0U || frame.height == 0U) {
            return PreviewRenderResult::InvalidFrame;
        }

        constexpr auto bytes_per_pixel = std::size_t{4};
        if (static_cast<std::size_t>(frame.width) >
            std::numeric_limits<std::size_t>::max() / bytes_per_pixel) {
            return PreviewRenderResult::InvalidFrame;
        }
        const auto minimum_row_pitch =
            static_cast<std::size_t>(frame.width) * bytes_per_pixel;
        if (static_cast<std::size_t>(frame.row_pitch) < minimum_row_pitch ||
            static_cast<std::size_t>(frame.height) >
                std::numeric_limits<std::size_t>::max() /
                    static_cast<std::size_t>(frame.row_pitch) ||
            frame.data_size < static_cast<std::size_t>(frame.row_pitch) *
                                  static_cast<std::size_t>(frame.height)) {
            return PreviewRenderResult::InvalidFrame;
        }
        if (target_window == nullptr || IsWindow(target_window) == FALSE) {
            return PreviewRenderResult::TargetUnavailable;
        }
        if (!ensure_upload_device() || !ensure_swap_chain(frame.width, frame.height)) {
            return PreviewRenderResult::Failed;
        }

        ComPtr<ID3D11Texture2D> back_buffer;
        const auto buffer_result = swap_chain->GetBuffer(
            0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back_buffer.GetAddressOf()));
        if (FAILED(buffer_result)) {
            return PreviewRenderResult::Failed;
        }

        context->UpdateSubresource(
            back_buffer.Get(), 0, nullptr, frame.data, frame.row_pitch, 0);
        return present();
    }

    [[nodiscard]] PreviewRenderResult present() {

        const auto present_result = swap_chain->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
        if (present_result == DXGI_ERROR_WAS_STILL_DRAWING ||
            present_result == DXGI_STATUS_OCCLUDED) {
            return PreviewRenderResult::Dropped;
        }
        if (present_result == DXGI_ERROR_DEVICE_REMOVED ||
            present_result == DXGI_ERROR_DEVICE_RESET) {
            release_device_resources();
            return PreviewRenderResult::DeviceLost;
        }
        return SUCCEEDED(present_result) ? PreviewRenderResult::Rendered
                                         : PreviewRenderResult::Failed;
    }
};

D3D11PreviewRenderer::D3D11PreviewRenderer(const std::uintptr_t target_window_handle)
    : implementation_(std::make_unique<Impl>(target_window_handle)) {
}

D3D11PreviewRenderer::~D3D11PreviewRenderer() {
    reset();
}

PreviewRenderResult D3D11PreviewRenderer::render(
    const screen_share::capture::D3D11CaptureFrameView& frame) noexcept {
    try {
        const std::scoped_lock lock(implementation_->mutex);
        return implementation_->render(frame);
    } catch (...) {
        return PreviewRenderResult::Failed;
    }
}

PreviewRenderResult D3D11PreviewRenderer::render(const BgraFrameView& frame) noexcept {
    try {
        const std::scoped_lock lock(implementation_->mutex);
        return implementation_->render(frame);
    } catch (...) {
        return PreviewRenderResult::Failed;
    }
}

void D3D11PreviewRenderer::reset() noexcept {
    if (implementation_) {
        const std::scoped_lock lock(implementation_->mutex);
        implementation_->release_device_resources();
    }
}

}  // namespace screen_share::renderer
