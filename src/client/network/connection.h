// connection.h
#pragma once

#include "shared/protocol/frame.h"
#include <asio.hpp>
#include <atomic>
#include <deque>
#include <functional>
#include <string>
#include <thread>

namespace mcg::client
{

class Connection
{
public:
    using MessageCallback = std::function<void(mcg::protocol::MessageType, nlohmann::json)>;

    Connection(const std::string& host, uint16_t port);
    ~Connection();

    void connect();
    void disconnect();
    void send(mcg::protocol::MessageType type, const nlohmann::json& payload);
    void set_message_callback(MessageCallback cb) { on_message_ = std::move(cb); }

private:
    void do_read();
    void do_write();

    std::string host_;
    uint16_t port_;
    asio::io_context io_context_;
    asio::ip::tcp::socket socket_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    mcg::protocol::FrameParser parser_;
    std::array<uint8_t, 4096> read_buffer_;
    std::deque<std::vector<uint8_t>> write_queue_;
    MessageCallback on_message_;
};

} // namespace mcg::client