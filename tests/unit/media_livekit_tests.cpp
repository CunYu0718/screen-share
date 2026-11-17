#include "screen_share/media/livekit_screen_publisher.hpp"
#include "screen_share/media/livekit_screen_subscriber.hpp"
#include "screen_share/media/raw_video_frame.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void check(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}  // namespace

int main() {
    using screen_share::media::BgraVideoFrame;

    BgraVideoFrame valid_frame{
        .width = 2,
        .height = 2,
        .timestamp_us = 1234,
        .pixels = std::vector<std::uint8_t>(16, 0x7f),
    };
    check(valid_frame.valid(), "BGRA frame validates its exact byte count");

    valid_frame.pixels.pop_back();
    check(!valid_frame.valid(), "BGRA frame rejects a truncated buffer");
    check(!BgraVideoFrame{}.valid(), "BGRA frame rejects empty dimensions");

    check(screen_share::media::is_valid_livekit_url("ws://localhost:7880"),
          "local WebSocket URL is accepted");
    check(screen_share::media::is_valid_livekit_url("wss://example.livekit.cloud"),
          "secure WebSocket URL is accepted");
    check(!screen_share::media::is_valid_livekit_url("https://example.com"),
          "HTTP URL is rejected");
    check(!screen_share::media::is_valid_livekit_url("wss://"),
          "URL without a host is rejected");

    screen_share::media::LiveKitScreenSubscriber subscriber;
    const auto invalid_subscribe = subscriber.start({});
    check(invalid_subscribe ==
              screen_share::media::LiveKitSubscribeStartResult::InvalidConfiguration,
          "subscriber rejects empty connection settings without network access");
    BgraVideoFrame received_frame;
    check(!subscriber.take_latest(received_frame),
          "subscriber has no frame before a room connection");
    subscriber.stop();

    std::cout << "All LiveKit media adapter tests passed\n";
    return EXIT_SUCCESS;
}
