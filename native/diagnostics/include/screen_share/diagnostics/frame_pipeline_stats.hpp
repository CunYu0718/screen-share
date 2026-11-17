#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace screen_share::diagnostics {

enum class PipelineStage : std::uint8_t {
    Capture,
    Encode,
    Send,
    Receive,
    Decode,
    Render,
    Count,
};

inline constexpr std::size_t pipeline_stage_count = static_cast<std::size_t>(PipelineStage::Count);

struct FramePipelineSnapshot {
    std::array<std::uint64_t, pipeline_stage_count> frame_counts{};

    [[nodiscard]] std::uint64_t count(PipelineStage stage) const noexcept;
};

class FramePipelineStats final {
public:
    FramePipelineStats() noexcept = default;

    FramePipelineStats(const FramePipelineStats&) = delete;
    FramePipelineStats& operator=(const FramePipelineStats&) = delete;
    FramePipelineStats(FramePipelineStats&&) = delete;
    FramePipelineStats& operator=(FramePipelineStats&&) = delete;

    void record(PipelineStage stage, std::uint64_t frames = 1) noexcept;
    [[nodiscard]] FramePipelineSnapshot snapshot() const noexcept;
    void reset() noexcept;

private:
    std::array<std::atomic<std::uint64_t>, pipeline_stage_count> frame_counts_{};
};

[[nodiscard]] std::string_view to_string(PipelineStage stage) noexcept;

}  // namespace screen_share::diagnostics
