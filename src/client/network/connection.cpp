// connection.cpp
#include "client/network/connection.h"
#include "shared/util/error.h"
#include "shared/util/logging.h"

namespace mcg::client
{

Connection::Connection(const std::string& host, uint16_t port)
    : host_(host), port_(port), socket_(io_context_)
{
}

Connection::~Connection()
{
    disconnect();
}

void Connection::connect()
{
    asio::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(host_, std::to_string(port_));
    asio::connect(socket_, endpoints);

    MCG_INFO("Connected to {}:{}", host_, port_);
    running_ = true;
    do_read();

    thread_ = std::thread([this]() { io_context_.run(); });
}

void Connection::disconnect()
{
    running_ = false;
    io_context_.stop();
    if (thread_.joinable())
    {
        thread_.join();
    }
    std::error_code ec;
    socket_.close(ec);
}

void Connection::send(mcg::protocol::MessageType type, const nlohmann::json& payload)
{
    auto frame = mcg::protocol::encode_frame(type, payload);
    asio::post(io_context_,
               [this, frame = std::move(frame)]() mutable
               {
                   bool write_in_progress = !write_queue_.empty();
                   write_queue_.push_back(std::move(frame));
                   if (!write_in_progress)
                   {
                       do_write();
                   }
               });
}

void Connection::do_read()
{
    socket_.async_read_some(asio::buffer(read_buffer_),
                            [this](std::error_code ec, std::size_t length)
                            {
                                if (!ec)
                                {
                                    parser_.feed(
                                        std::span<const uint8_t>(read_buffer_.data(), length));
                                    while (auto msg = parser_.next())
                                    {
                                        if (on_message_)
                                        {
                                            on_message_(msg->first, msg->second);
                                        }
                                    }
                                    do_read();
                                }
                                else
                                {
                                    if (running_)
                                    {
                                        // »—œ–¿¬À≈ÕŒ
                                        MCG_ERROR("Read error: {}", mcg::util::error_message(ec));
                                    }
                                }
                            });
}

void Connection::do_write()
{
    asio::async_write(socket_, asio::buffer(write_queue_.front()),
                      [this](std::error_code ec, std::size_t /*length*/)
                      {
                          if (!ec)
                          {
                              write_queue_.pop_front();
                              if (!write_queue_.empty())
                              {
                                  do_write();
                              }
                          }
                          else
                          {
                              // »—œ–¿¬À≈ÕŒ
                              MCG_ERROR("Write error: {}", mcg::util::error_message(ec));
                          }
                      });
}
} // namespace mcg::client