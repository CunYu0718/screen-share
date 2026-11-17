#pragma once

#include "screen_share/capture/wgc_capture_session.hpp"

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
    void reset() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace screen_share::renderer
