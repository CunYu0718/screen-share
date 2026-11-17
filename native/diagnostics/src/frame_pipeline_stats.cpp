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

void FramePipelineStats::record(const PipelineStage stage, const std::uint64_t frames) noexcept {
    const auto index = to_index(stage);
    assert(index < frame_counts_.size());

    if (index < frame_counts_.size()) {
        frame_counts_[index].fetch_add(frames, std::memory_order_relaxed);
    }
}

FramePipelineSnapshot FramePipelineStats::snapshot() const noexcept {
    FramePipelineSnapshot result;

    for (std::size_t index = 0; index < frame_counts_.size(); ++index) {
        result.frame_counts[index] = frame_counts_[index].load(std::memory_order_relaxed);
    }

    return result;
}

void FramePipelineStats::reset() noexcept {
    for (auto& count : frame_counts_) {
        count.store(0, std::memory_order_relaxed);
    }
}

std::string_view to_string(const PipelineStage stage) noexcept {
    const auto index = to_index(stage);
    return index < stage_names.size() ? stage_names[index] : "unknown";
}

}  // namespace screen_share::diagnostics
