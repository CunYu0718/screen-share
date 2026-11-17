#pragma once

#include <string>

namespace screen_share::media {

[[nodiscard]] bool is_valid_livekit_url(const std::string& url) noexcept;

}  // namespace screen_share::media
