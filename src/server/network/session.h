// session.h
#pragma once

#include "shared/protocol/frame.h"
#include <asio.hpp>
#include <deque>
#include <functional>
#include <memory>

namespace mcg::server
{

class Session : public std::enable_shared_from_this<Session>
{
public:
    using MessageCallback =
        std::function<void(std::shared_ptr<Session>, mcg::protocol::MessageType, nlohmann::json)>;

    Session(asio::ip::tcp::socket socket, uint64_t id, MessageCallback on_message);
    ~Session();

    void start();
    void send(mcg::protocol::MessageType type, const nlohmann::json& payload);
    uint64_t id() const { return id_; }

private:
    void do_read();
    void do_write();

    uint64_t id_;
    asio::ip::tcp::socket socket_;
    MessageCallback on_message_;
    mcg::protocol::FrameParser parser_;
    std::array<uint8_t, 4096> read_buffer_;
    std::deque<std::vector<uint8_t>> write_queue_;
};

} // namespace mcg::server