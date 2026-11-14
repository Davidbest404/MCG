#include "server/network/network_thread.h"
#include "shared/util/error.h"
#include "shared/util/logging.h"

namespace mcg::server
{

NetworkThread::NetworkThread(uint16_t port) : port_(port), io_context_(), acceptor_(io_context_)
{
    asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port);

    // 1. Открываем сокет для TCP
    acceptor_.open(endpoint.protocol());

    // 2. КРИТИЧНО: Запрещаем двум приложениям слушать один порт.
    // На Windows это включает SO_EXCLUSIVEADDRUSE.
    // Если порт занят, следующие шаги выбросят исключение.
    acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(false));

    // 3. Привязываемся к порту (здесь произойдёт ошибка, если порт занят)
    acceptor_.bind(endpoint);

    // 4. Начинаем слушать
    acceptor_.listen();
}

NetworkThread::~NetworkThread()
{
    stop();
}

uint16_t NetworkThread::port() const
{
    return acceptor_.local_endpoint().port();
}

void NetworkThread::start()
{
    do_accept();
    thread_ = std::thread(
        [this]()
        {
            MCG_INFO("Network thread started on port {}", port());
            io_context_.run();
        });
}

void NetworkThread::stop()
{
    io_context_.stop();
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void NetworkThread::send(std::shared_ptr<Session> session, mcg::protocol::MessageType type,
                         const nlohmann::json& payload)
{
    asio::post(io_context_, [session, type, payload]() { session->send(type, payload); });
}

bool NetworkThread::pop_message(IncomingMessage& out_msg)
{
    return incoming_queue_.pop(out_msg);
}

void NetworkThread::do_accept()
{
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket)
        {
            if (!ec)
            {
                static std::atomic<uint64_t> next_id{1};
                auto session = std::make_shared<Session>(
                    std::move(socket), next_id.fetch_add(1),
                    [this](std::shared_ptr<Session> s, mcg::protocol::MessageType type,
                           nlohmann::json payload)
                    { incoming_queue_.push({s, type, std::move(payload)}); });
                session->start();
            }
            else
            {
                MCG_ERROR("Accept error: {}", mcg::util::error_message(ec));
            }
            do_accept();
        });
}

} // namespace mcg::server