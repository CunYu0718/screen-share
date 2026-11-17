#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace screen_share::core {

enum class QualityTier : std::uint8_t {
    Smooth,
    FullHd,
    QuadHd,
};

struct QualityProfile {
    QualityTier tier;
    std::string_view name;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t frames_per_second;
    std::uint32_t minimum_bitrate_bps;
    std::uint32_t maximum_bitrate_bps;

    friend constexpr bool operator==(const QualityProfile&, const QualityProfile&) = default;
};

[[nodiscard]] std::span<const QualityProfile> quality_profiles() noexcept;
[[nodiscard]] std::optional<QualityProfile> quality_profile_for(QualityTier tier) noexcept;

}  // namespace screen_share::core
