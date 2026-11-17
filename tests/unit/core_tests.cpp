#include "screen_share/core/quality_profile.hpp"
#include "screen_share/core/session_policy.hpp"
#include "screen_share/diagnostics/frame_rate_estimator.hpp"
#include "screen_share/diagnostics/frame_pipeline_stats.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}  // namespace

int main() {
    using screen_share::core::ParticipantRole;
    using screen_share::core::QualityTier;
    using screen_share::core::ViewerAdmission;
    using screen_share::diagnostics::FramePipelineStats;
    using screen_share::diagnostics::FrameRateEstimator;
    using screen_share::diagnostics::PipelineStage;

    static_assert(screen_share::core::maximum_viewer_count == 20);
    static_assert(screen_share::core::maximum_participant_count == 21);
    static_assert(screen_share::core::admit_viewer(19) == ViewerAdmission::Accepted);
    static_assert(screen_share::core::admit_viewer(20) == ViewerAdmission::RoomFull);
    static_assert(screen_share::core::can_publish_screen(ParticipantRole::Host));
    static_assert(!screen_share::core::can_publish_screen(ParticipantRole::Viewer));
    static_assert(screen_share::core::can_subscribe_screen(ParticipantRole::Viewer));

    const auto profiles = screen_share::core::quality_profiles();
    check(profiles.size() == 3, "three quality profiles are defined");

    const auto quad_hd = screen_share::core::quality_profile_for(QualityTier::QuadHd);
    check(quad_hd.has_value(), "1440p profile exists");
    check(quad_hd->width == 2560, "1440p profile width");
    check(quad_hd->height == 1440, "1440p profile height");
    check(quad_hd->frames_per_second == 60, "1440p profile frame rate");

    FramePipelineStats stats;
    stats.record(PipelineStage::Capture, 120);
    stats.record(PipelineStage::Encode, 118);
    stats.record(PipelineStage::Render, 110);
    stats.record_duration(PipelineStage::Render, 2'000'000);
    stats.record_duration(PipelineStage::Render, 4'000'000);
    stats.set_duration(PipelineStage::Encode, 118'000'000, 118);

    const auto snapshot = stats.snapshot();
    check(snapshot.count(PipelineStage::Capture) == 120, "capture frame count");
    check(snapshot.count(PipelineStage::Encode) == 118, "encode frame count");
    check(snapshot.count(PipelineStage::Render) == 110, "render frame count");
    check(snapshot.average_duration_milliseconds(PipelineStage::Render) == 3.0,
          "render average duration");
    check(snapshot.average_duration_milliseconds(PipelineStage::Encode) == 1.0,
          "authoritative encode duration");
    check(screen_share::diagnostics::to_string(PipelineStage::Decode) == "decode", "stage name");

    stats.reset();
    check(stats.snapshot().count(PipelineStage::Capture) == 0, "counter reset");
    check(stats.snapshot().average_duration_milliseconds(PipelineStage::Render) == 0.0,
          "duration reset");

    FramePipelineStats rate_stats;
    screen_share::diagnostics::FramePipelineRateEstimator pipeline_rate;
    rate_stats.set_count(PipelineStage::Capture, 10);
    check(pipeline_rate.sample(rate_stats.snapshot(), 10'000'000).rate(
              PipelineStage::Capture) == 0.0,
          "first pipeline sample establishes a baseline");
    rate_stats.set_count(PipelineStage::Capture, 70);
    rate_stats.set_count(PipelineStage::Encode, 55);
    const auto rates = pipeline_rate.sample(rate_stats.snapshot(), 20'000'000);
    check(rates.rate(PipelineStage::Capture) == 60.0, "capture pipeline rate");
    check(rates.rate(PipelineStage::Encode) == 55.0, "encode pipeline rate");
    pipeline_rate.reset();

    FrameRateEstimator frame_rate;
    frame_rate.record(0);
    frame_rate.record(166'667);
    frame_rate.record(333'334);
    const auto rate_sample = frame_rate.sample();
    check(rate_sample.total_frames == 3, "frame rate sample tracks total frames");
    check(rate_sample.frames_per_second > 59.9 && rate_sample.frames_per_second < 60.1,
          "frame rate sample uses QPC time intervals");
    check(frame_rate.sample().frames_per_second == 0.0, "idle sample reports zero fps");
    frame_rate.reset();
    check(frame_rate.sample().total_frames == 0, "frame rate reset clears totals");

    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
