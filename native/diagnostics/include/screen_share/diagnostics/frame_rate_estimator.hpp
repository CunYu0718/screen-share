#pragma once

#include <cstdint>
#include <mutex>

namespace screen_share::diagnostics {

struct FrameRateSample {
    std::uint64_t total_frames;
    double frames_per_second;
    std::int64_t sample_duration_100ns;
};

class FrameRateEstimator final {
public:
    FrameRateEstimator() = default;

    FrameRateEstimator(const FrameRateEstimator&) = delete;
    FrameRateEstimator& operator=(const FrameRateEstimator&) = delete;
    FrameRateEstimator(FrameRateEstimator&&) = delete;
    FrameRateEstimator& operator=(FrameRateEstimator&&) = delete;

    void record(std::int64_t system_relative_time_100ns);
    [[nodiscard]] FrameRateSample sample();
    void reset();

private:
    std::mutex mutex_;
    std::uint64_t total_frames_{0};
    std::uint64_t sample_start_frame_count_{0};
    std::int64_t sample_start_time_100ns_{0};
    std::int64_t latest_time_100ns_{0};
    bool has_timestamp_{false};
};

}  // namespace screen_share::diagnostics
