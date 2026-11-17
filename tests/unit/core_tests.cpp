#include "screen_share/core/quality_profile.hpp"
#include "screen_share/core/session_policy.hpp"
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

    const auto snapshot = stats.snapshot();
    check(snapshot.count(PipelineStage::Capture) == 120, "capture frame count");
    check(snapshot.count(PipelineStage::Encode) == 118, "encode frame count");
    check(snapshot.count(PipelineStage::Render) == 110, "render frame count");
    check(screen_share::diagnostics::to_string(PipelineStage::Decode) == "decode", "stage name");

    stats.reset();
    check(stats.snapshot().count(PipelineStage::Capture) == 0, "counter reset");

    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
