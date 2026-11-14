#include <catch2/catch_test_macros.hpp>

#include "shared/protocol/frame.h"

using namespace mcg::protocol;
using nlohmann::json;

TEST_CASE("Encode frame has correct layout", "[protocol]")
{
    const json payload = {{"msg", "hello"}};
    const auto buf = encode_frame(MessageType::Ping, payload);

    REQUIRE(buf.size() >= 6);
    // frame_size = 4 + 2 + payload.size()
    const uint32_t size = static_cast<uint32_t>(buf[0]) | (static_cast<uint32_t>(buf[1]) << 8) |
                          (static_cast<uint32_t>(buf[2]) << 16) |
                          (static_cast<uint32_t>(buf[3]) << 24);
    REQUIRE(size == buf.size());

    // type
    const uint16_t t = static_cast<uint16_t>(buf[4]) | (static_cast<uint16_t>(buf[5]) << 8);
    REQUIRE(t == static_cast<uint16_t>(MessageType::Ping));
}

TEST_CASE("Round-trip one frame", "[protocol]")
{
    const json payload = {{"a", 1}, {"b", "text"}};
    const auto buf = encode_frame(MessageType::ChatMessage, payload);

    FrameParser p;
    p.feed(buf);

    const auto f = p.next();
    REQUIRE(f.has_value());
    REQUIRE(f->first == MessageType::ChatMessage);
    REQUIRE(f->second == payload);

    REQUIRE_FALSE(p.next().has_value());
    REQUIRE(p.buffered() == 0);
}

TEST_CASE("Two frames in one buffer", "[protocol]")
{
    const auto b1 = encode_frame(MessageType::Ping, json{{"n", 1}});
    const auto b2 = encode_frame(MessageType::Pong, json{{"n", 2}});

    std::vector<uint8_t> combined = b1;
    combined.insert(combined.end(), b2.begin(), b2.end());

    FrameParser p;
    p.feed(combined);

    auto f1 = p.next();
    REQUIRE(f1.has_value());
    REQUIRE(f1->first == MessageType::Ping);

    auto f2 = p.next();
    REQUIRE(f2.has_value());
    REQUIRE(f2->first == MessageType::Pong);

    REQUIRE_FALSE(p.next().has_value());
}

TEST_CASE("Partial feed reconstructs frame", "[protocol]")
{
    const json payload = {{"hello", "world"}};
    const auto buf = encode_frame(MessageType::ChatMessage, payload);

    FrameParser p;
    // Кормим по 2 байта.
    for (std::size_t i = 0; i < buf.size(); i += 2)
    {
        const std::size_t end = std::min(i + 2, buf.size());
        p.feed(std::span<const uint8_t>(buf.data() + i, end - i));
    }

    const auto f = p.next();
    REQUIRE(f.has_value());
    REQUIRE(f->first == MessageType::ChatMessage);
    REQUIRE(f->second == payload);
}

TEST_CASE("Corrupted frame resets buffer", "[protocol]")
{
    // frame_size = 1 — невалидно (< 6).
    std::vector<uint8_t> bad = {1, 0, 0, 0, 0, 0};
    FrameParser p;
    p.feed(bad);

    REQUIRE_FALSE(p.next().has_value());
    REQUIRE(p.buffered() == 0);
}