#pragma once

#include "server/network/session.h"
#include "shared/thread/lockfree_queue.h"
#include <asio.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <tuple>

namespace mcg::server
{

class NetworkThread
{
public:
    using IncomingMessage =
        std::tuple<std::shared_ptr<Session>, mcg::protocol::MessageType, nlohmann::json>;

    NetworkThread(uint16_t port);
    ~NetworkThread();

    void start();
    void stop();

    // Возвращает реальный порт, на который привязался сервер.
    // Если при создании передан 0, ОС выберет свободный порт.
    uint16_t port() const;

    void send(std::shared_ptr<Session> session, mcg::protocol::MessageType type,
              const nlohmann::json& payload);
    bool pop_message(IncomingMessage& out_msg);

private:
    void do_accept();

    uint16_t port_;
    asio::io_context io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread thread_;

    mcg::thread::LockFreeQueue<IncomingMessage, 4096> incoming_queue_;
};

} // namespace mcg::server