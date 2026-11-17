#include "screen_share/diagnostics/frame_rate_estimator.hpp"

#include <cstdint>

namespace screen_share::diagnostics {
namespace {

inline constexpr double ticks_per_second = 10'000'000.0;

}  // namespace

void FrameRateEstimator::record(const std::int64_t system_relative_time_100ns) {
    const std::scoped_lock lock(mutex_);
    ++total_frames_;

    if (!has_timestamp_ || system_relative_time_100ns < latest_time_100ns_) {
        has_timestamp_ = true;
        sample_start_frame_count_ = total_frames_;
        sample_start_time_100ns_ = system_relative_time_100ns;
    }

    latest_time_100ns_ = system_relative_time_100ns;
}

FrameRateSample FrameRateEstimator::sample() {
    const std::scoped_lock lock(mutex_);
    const auto duration = latest_time_100ns_ - sample_start_time_100ns_;
    const auto frame_intervals = total_frames_ - sample_start_frame_count_;
    const auto rate = has_timestamp_ && duration > 0 && frame_intervals > 0
                          ? static_cast<double>(frame_intervals) * ticks_per_second /
                                static_cast<double>(duration)
                          : 0.0;

    const FrameRateSample result{
        .total_frames = total_frames_,
        .frames_per_second = rate,
        .sample_duration_100ns = duration > 0 ? duration : 0,
    };
    sample_start_frame_count_ = total_frames_;
    sample_start_time_100ns_ = latest_time_100ns_;
    return result;
}

void FrameRateEstimator::reset() {
    const std::scoped_lock lock(mutex_);
    total_frames_ = 0;
    sample_start_frame_count_ = 0;
    sample_start_time_100ns_ = 0;
    latest_time_100ns_ = 0;
    has_timestamp_ = false;
}

}  // namespace screen_share::diagnostics
