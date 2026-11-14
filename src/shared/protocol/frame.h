#pragma once

#include "shared/protocol/message_type.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace mcg::protocol
{

// Максимальный размер одного фрейма. Защита от мусора и DoS.
constexpr uint32_t kMaxFrameSize = 16u * 1024u * 1024u; // 16 MB

// Кодирует фрейм в байтовый буфер.
std::vector<uint8_t> encode_frame(MessageType type, const nlohmann::json& payload);

// Инкрементальный парсер входящего потока.
class FrameParser
{
public:
    // Добавляет байты в буфер.
    void feed(std::span<const uint8_t> data);

    // Возвращает следующий готовый фрейм, если он есть.
    // Если данных недостаточно — nullopt; нужно ещё feed().
    std::optional<std::pair<MessageType, nlohmann::json>> next();

    // Сброс буфера (например, при переподключении).
    void reset();

    // Сколько байт в буфере (диагностика).
    std::size_t buffered() const { return buffer_.size(); }

private:
    std::vector<uint8_t> buffer_;
};

} // namespace mcg::protocol