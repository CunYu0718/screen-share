#pragma once

#include <cstddef>
#include <cstdint>

namespace screen_share::core {

inline constexpr std::size_t maximum_viewer_count = 20;
inline constexpr std::size_t maximum_participant_count = maximum_viewer_count + 1;

enum class ParticipantRole : std::uint8_t {
    Host,
    Viewer,
};

enum class ViewerAdmission : std::uint8_t {
    Accepted,
    RoomFull,
};

[[nodiscard]] constexpr ViewerAdmission admit_viewer(const std::size_t current_viewer_count) noexcept {
    return current_viewer_count < maximum_viewer_count ? ViewerAdmission::Accepted : ViewerAdmission::RoomFull;
}

[[nodiscard]] constexpr bool can_publish_screen(const ParticipantRole role) noexcept {
    return role == ParticipantRole::Host;
}

[[nodiscard]] constexpr bool can_subscribe_screen(const ParticipantRole role) noexcept {
    return role == ParticipantRole::Viewer;
}

}  // namespace screen_share::core
