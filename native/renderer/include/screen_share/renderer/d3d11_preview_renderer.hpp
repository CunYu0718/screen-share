#pragma once

#include "screen_share/capture/wgc_capture_session.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace screen_share::renderer {

enum class PreviewRenderResult : std::uint8_t {
    Rendered,
    Dropped,
    InvalidFrame,
    TargetUnavailable,
    DeviceLost,
    Failed,
};

struct BgraFrameView {
    const std::uint8_t* data;
    std::size_t data_size;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t row_pitch;
};

class D3D11PreviewRenderer final {
public:
    explicit D3D11PreviewRenderer(std::uintptr_t target_window_handle);
    ~D3D11PreviewRenderer();

    D3D11PreviewRenderer(const D3D11PreviewRenderer&) = delete;
    D3D11PreviewRenderer& operator=(const D3D11PreviewRenderer&) = delete;
    D3D11PreviewRenderer(D3D11PreviewRenderer&&) = delete;
    D3D11PreviewRenderer& operator=(D3D11PreviewRenderer&&) = delete;

    [[nodiscard]] PreviewRenderResult render(
        const screen_share::capture::D3D11CaptureFrameView& frame) noexcept;
    [[nodiscard]] PreviewRenderResult render(const BgraFrameView& frame) noexcept;
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace screen_share::renderer
