#pragma once
#include <cstdint>

namespace mcg::protocol
{
enum class MessageType : uint16_t
{
    Unknown = 0,
    Ping = 1,
    Pong = 2,
    AuthRequest = 10,
    AuthChallenge = 11,
    AuthResponse = 12,
    AuthResult = 13,
    ChatMessage = 20,
    Command = 21,
    CommandResult = 22,
    EntityUpdate = 30,
    EntityRemove = 31,
    WorldSnapshot = 40, // Ñòàğûé ôîğìàò (ñòğîêè)
    ChunkData = 41,

    // ÍÎÂÛÅ ÑÎÎÁÙÅÍÈß ÄËß ĞÅÍÄÅĞÀ
    RenderConfig = 50,
    RenderRequest = 51,
    WorldRender = 52,

    Error = 100,
};

inline const char* to_string(MessageType t)
{
    switch (t)
    {
    case MessageType::Unknown:
        return "Unknown";
    case MessageType::Ping:
        return "Ping";
    case MessageType::Pong:
        return "Pong";
    case MessageType::AuthRequest:
        return "AuthRequest";
    case MessageType::AuthChallenge:
        return "AuthChallenge";
    case MessageType::AuthResponse:
        return "AuthResponse";
    case MessageType::AuthResult:
        return "AuthResult";
    case MessageType::ChatMessage:
        return "ChatMessage";
    case MessageType::Command:
        return "Command";
    case MessageType::CommandResult:
        return "CommandResult";
    case MessageType::EntityUpdate:
        return "EntityUpdate";
    case MessageType::EntityRemove:
        return "EntityRemove";
    case MessageType::WorldSnapshot:
        return "WorldSnapshot";
    case MessageType::ChunkData:
        return "ChunkData";
    case MessageType::RenderConfig:
        return "RenderConfig";
    case MessageType::RenderRequest:
        return "RenderRequest";
    case MessageType::WorldRender:
        return "WorldRender";
    case MessageType::Error:
        return "Error";
    default:
        return "??";
    }
}
} // namespace mcg::protocol