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
    std::array<std::uint64_t, pipeline_stage_count> total_duration_nanoseconds{};
    std::array<std::uint64_t, pipeline_stage_count> duration_samples{};

    [[nodiscard]] std::uint64_t count(PipelineStage stage) const noexcept;
    [[nodiscard]] double average_duration_milliseconds(PipelineStage stage) const noexcept;
};

struct FramePipelineRateSample {
    std::array<double, pipeline_stage_count> frames_per_second{};

    [[nodiscard]] double rate(PipelineStage stage) const noexcept;
};

class FramePipelineRateEstimator final {
public:
    [[nodiscard]] FramePipelineRateSample sample(
        const FramePipelineSnapshot& snapshot,
        std::int64_t timestamp_100ns) noexcept;
    void reset() noexcept;

private:
    FramePipelineSnapshot previous_snapshot_{};
    std::int64_t previous_timestamp_100ns_{0};
    bool initialized_{false};
};

class FramePipelineStats final {
public:
    FramePipelineStats() noexcept = default;

    FramePipelineStats(const FramePipelineStats&) = delete;
    FramePipelineStats& operator=(const FramePipelineStats&) = delete;
    FramePipelineStats(FramePipelineStats&&) = delete;
    FramePipelineStats& operator=(FramePipelineStats&&) = delete;

    void record(PipelineStage stage, std::uint64_t frames = 1) noexcept;
    void record_duration(PipelineStage stage, std::uint64_t duration_nanoseconds) noexcept;
    void set_count(PipelineStage stage, std::uint64_t frames) noexcept;
    void set_duration(PipelineStage stage,
                      std::uint64_t total_duration_nanoseconds,
                      std::uint64_t samples) noexcept;
    [[nodiscard]] FramePipelineSnapshot snapshot() const noexcept;
    void reset() noexcept;

private:
    std::array<std::atomic<std::uint64_t>, pipeline_stage_count> frame_counts_{};
    std::array<std::atomic<std::uint64_t>, pipeline_stage_count>
        total_duration_nanoseconds_{};
    std::array<std::atomic<std::uint64_t>, pipeline_stage_count> duration_samples_{};
};

[[nodiscard]] std::string_view to_string(PipelineStage stage) noexcept;

}  // namespace screen_share::diagnostics
