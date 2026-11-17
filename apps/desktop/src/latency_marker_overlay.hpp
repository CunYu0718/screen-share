#pragma once

#include <cstdint>
#include <memory>

namespace screen_share::desktop {

class LatencyMarkerOverlay final {
public:
    LatencyMarkerOverlay();
    ~LatencyMarkerOverlay();

    LatencyMarkerOverlay(const LatencyMarkerOverlay&) = delete;
    LatencyMarkerOverlay& operator=(const LatencyMarkerOverlay&) = delete;
    LatencyMarkerOverlay(LatencyMarkerOverlay&&) = delete;
    LatencyMarkerOverlay& operator=(LatencyMarkerOverlay&&) = delete;

    [[nodiscard]] bool show_on_monitor(std::uintptr_t monitor_handle) noexcept;
    void update_timestamp(std::uint64_t utc_timestamp_milliseconds) noexcept;
    void hide() noexcept;
    [[nodiscard]] bool visible() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace screen_share::desktop
