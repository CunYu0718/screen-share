#include "screen_share/media/livekit_url.hpp"

#include <string>

namespace screen_share::media {

bool is_valid_livekit_url(const std::string& url) noexcept {
    constexpr auto insecure_prefix = "ws://";
    constexpr auto secure_prefix = "wss://";
    return (url.starts_with(insecure_prefix) &&
            url.size() > std::char_traits<char>::length(insecure_prefix)) ||
           (url.starts_with(secure_prefix) &&
            url.size() > std::char_traits<char>::length(secure_prefix));
}

}  // namespace screen_share::media
