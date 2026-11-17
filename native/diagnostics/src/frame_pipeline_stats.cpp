#include "screen_share/diagnostics/frame_pipeline_stats.hpp"

#include <array>
#include <cassert>

namespace screen_share::diagnostics {
namespace {

[[nodiscard]] constexpr std::size_t to_index(const PipelineStage stage) noexcept {
    return static_cast<std::size_t>(stage);
}

constexpr std::array<std::string_view, pipeline_stage_count> stage_names{
    "capture",
    "encode",
    "send",
    "receive",
    "decode",
    "render",
};

}  // namespace

std::uint64_t FramePipelineSnapshot::count(const PipelineStage stage) const noexcept {
    const auto index = to_index(stage);
    assert(index < frame_counts.size());
    return index < frame_counts.size() ? frame_counts[index] : 0;
}

double FramePipelineSnapshot::average_duration_milliseconds(
    const PipelineStage stage) const noexcept {
    const auto index = to_index(stage);
    assert(index < duration_samples.size());
    if (index >= duration_samples.size() || duration_samples[index] == 0U) {
        return 0.0;
    }
    constexpr double nanoseconds_per_millisecond = 1'000'000.0;
    return static_cast<double>(total_duration_nanoseconds[index]) /
           static_cast<double>(duration_samples[index]) / nanoseconds_per_millisecond;
}

double FramePipelineRateSample::rate(const PipelineStage stage) const noexcept {
    const auto index = to_index(stage);
    assert(index < frames_per_second.size());
    return index < frames_per_second.size() ? frames_per_second[index] : 0.0;
}

FramePipelineRateSample FramePipelineRateEstimator::sample(
    const FramePipelineSnapshot& snapshot,
    const std::int64_t timestamp_100ns) noexcept {
    FramePipelineRateSample result;
    if (!initialized_ || timestamp_100ns <= previous_timestamp_100ns_) {
        previous_snapshot_ = snapshot;
        previous_timestamp_100ns_ = timestamp_100ns;
        initialized_ = true;
        return result;
    }

    constexpr double intervals_per_second = 10'000'000.0;
    const auto elapsed_intervals = timestamp_100ns - previous_timestamp_100ns_;
    const auto elapsed_seconds = static_cast<double>(elapsed_intervals) / intervals_per_second;
    for (std::size_t index = 0; index < pipeline_stage_count; ++index) {
        const auto current = snapshot.frame_counts[index];
        const auto previous = previous_snapshot_.frame_counts[index];
        const auto delta = current >= previous ? current - previous : current;
        result.frames_per_second[index] = static_cast<double>(delta) / elapsed_seconds;
    }

    previous_snapshot_ = snapshot;
    previous_timestamp_100ns_ = timestamp_100ns;
    return result;
}

void FramePipelineRateEstimator::reset() noexcept {
    previous_snapshot_ = {};
    previous_timestamp_100ns_ = 0;
    initialized_ = false;
}

void FramePipelineStats::record(const PipelineStage stage, const std::uint64_t frames) noexcept {
    const auto index = to_index(stage);
    assert(index < frame_counts_.size());

    if (index < frame_counts_.size()) {
        frame_counts_[index].fetch_add(frames, std::memory_order_relaxed);
    }
}

void FramePipelineStats::record_duration(
    const PipelineStage stage, const std::uint64_t duration_nanoseconds) noexcept {
    const auto index = to_index(stage);
    assert(index < total_duration_nanoseconds_.size());
    if (index < total_duration_nanoseconds_.size()) {
        total_duration_nanoseconds_[index].fetch_add(
            duration_nanoseconds, std::memory_order_relaxed);
        duration_samples_[index].fetch_add(1, std::memory_order_relaxed);
    }
}

void FramePipelineStats::set_count(
    const PipelineStage stage, const std::uint64_t frames) noexcept {
    const auto index = to_index(stage);
    assert(index < frame_counts_.size());
    if (index < frame_counts_.size()) {
        frame_counts_[index].store(frames, std::memory_order_relaxed);
    }
}

void FramePipelineStats::set_duration(
    const PipelineStage stage,
    const std::uint64_t total_duration_nanoseconds,
    const std::uint64_t samples) noexcept {
    const auto index = to_index(stage);
    assert(index < total_duration_nanoseconds_.size());
    if (index < total_duration_nanoseconds_.size()) {
        total_duration_nanoseconds_[index].store(
            total_duration_nanoseconds, std::memory_order_relaxed);
        duration_samples_[index].store(samples, std::memory_order_relaxed);
    }
}

FramePipelineSnapshot FramePipelineStats::snapshot() const noexcept {
    FramePipelineSnapshot result;

    for (std::size_t index = 0; index < frame_counts_.size(); ++index) {
        result.frame_counts[index] = frame_counts_[index].load(std::memory_order_relaxed);
        result.total_duration_nanoseconds[index] =
            total_duration_nanoseconds_[index].load(std::memory_order_relaxed);
        result.duration_samples[index] =
            duration_samples_[index].load(std::memory_order_relaxed);
    }

    return result;
}

void FramePipelineStats::reset() noexcept {
    for (auto& count : frame_counts_) {
        count.store(0, std::memory_order_relaxed);
    }
    for (auto& duration : total_duration_nanoseconds_) {
        duration.store(0, std::memory_order_relaxed);
    }
    for (auto& samples : duration_samples_) {
        samples.store(0, std::memory_order_relaxed);
    }
}

std::string_view to_string(const PipelineStage stage) noexcept {
    const auto index = to_index(stage);
    return index < stage_names.size() ? stage_names[index] : "unknown";
}

}  // namespace screen_share::diagnostics
