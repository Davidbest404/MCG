#include "shared/protocol/frame.h"

#include <cstring>
#include <exception>

namespace mcg::protocol
{

std::vector<uint8_t> encode_frame(MessageType type, const nlohmann::json& payload)
{
    // »—ѕ–ј¬Ћ≈Ќќ: error_handler_t::replace защищает от краша на русских буквах
    const std::string body = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const uint32_t total_size = static_cast<uint32_t>(4u + 2u + body.size());

    std::vector<uint8_t> out(total_size);

    out[0] = static_cast<uint8_t>(total_size & 0xFFu);
    out[1] = static_cast<uint8_t>((total_size >> 8) & 0xFFu);
    out[2] = static_cast<uint8_t>((total_size >> 16) & 0xFFu);
    out[3] = static_cast<uint8_t>((total_size >> 24) & 0xFFu);

    const uint16_t t = static_cast<uint16_t>(type);
    out[4] = static_cast<uint8_t>(t & 0xFFu);
    out[5] = static_cast<uint8_t>((t >> 8) & 0xFFu);

    if (!body.empty())
    {
        std::memcpy(out.data() + 6, body.data(), body.size());
    }

    return out;
}

void FrameParser::feed(std::span<const uint8_t> data)
{
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

std::optional<std::pair<MessageType, nlohmann::json>> FrameParser::next()
{
    if (buffer_.size() < 6) return std::nullopt;

    const uint32_t total_size =
        static_cast<uint32_t>(buffer_[0]) | (static_cast<uint32_t>(buffer_[1]) << 8) |
        (static_cast<uint32_t>(buffer_[2]) << 16) | (static_cast<uint32_t>(buffer_[3]) << 24);

    if (total_size < 6 || total_size > kMaxFrameSize)
    {
        // Ѕитый фрейм Ч сбрасываем буфер, продолжать бессмысленно.
        buffer_.clear();
        return std::nullopt;
    }

    if (buffer_.size() < total_size) return std::nullopt;

    const uint16_t type_raw =
        static_cast<uint16_t>(buffer_[4]) | (static_cast<uint16_t>(buffer_[5]) << 8);

    nlohmann::json payload;
    try
    {
        payload = nlohmann::json::parse(buffer_.begin() + 6, buffer_.begin() + total_size);
    }
    catch (const std::exception&)
    {
        payload = nlohmann::json::object();
    }

    buffer_.erase(buffer_.begin(), buffer_.begin() + total_size);

    return std::make_pair(static_cast<MessageType>(type_raw), std::move(payload));
}

void FrameParser::reset()
{
    buffer_.clear();
}

} // namespace mcg::protocol