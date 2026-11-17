#include "screen_share/core/quality_profile.hpp"

#include <array>

namespace screen_share::core {
namespace {

constexpr std::array<QualityProfile, 3> profiles{{
    {
        .tier = QualityTier::Smooth,
        .name = "720p30",
        .width = 1280,
        .height = 720,
        .frames_per_second = 30,
        .minimum_bitrate_bps = 2'000'000,
        .maximum_bitrate_bps = 4'000'000,
    },
    {
        .tier = QualityTier::FullHd,
        .name = "1080p60",
        .width = 1920,
        .height = 1080,
        .frames_per_second = 60,
        .minimum_bitrate_bps = 6'000'000,
        .maximum_bitrate_bps = 10'000'000,
    },
    {
        .tier = QualityTier::QuadHd,
        .name = "1440p60",
        .width = 2560,
        .height = 1440,
        .frames_per_second = 60,
        .minimum_bitrate_bps = 12'000'000,
        .maximum_bitrate_bps = 20'000'000,
    },
}};

}  // namespace

std::span<const QualityProfile> quality_profiles() noexcept {
    return profiles;
}

std::optional<QualityProfile> quality_profile_for(const QualityTier tier) noexcept {
    for (const auto& profile : profiles) {
        if (profile.tier == tier) {
            return profile;
        }
    }

    return std::nullopt;
}

}  // namespace screen_share::core
