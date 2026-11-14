// session.cpp
#include "server/network/session.h"
#include "shared/util/logging.h"
#include "shared/util/error.h"

namespace mcg::server
{

Session::Session(asio::ip::tcp::socket socket, uint64_t id, MessageCallback on_message)
    : socket_(std::move(socket)), id_(id), on_message_(std::move(on_message))
{
}

Session::~Session()
{
    std::error_code ec;
    socket_.close(ec);
}

void Session::start()
{
    MCG_INFO("Session {} started", id_);
    do_read();
}

void Session::send(mcg::protocol::MessageType type, const nlohmann::json& payload)
{
    auto frame = mcg::protocol::encode_frame(type, payload);
    bool write_in_progress = !write_queue_.empty();
    write_queue_.push_back(std::move(frame));
    if (!write_in_progress)
    {
        do_write();
    }
}

void Session::do_read()
{
    auto self = shared_from_this();
    socket_.async_read_some(
        asio::buffer(read_buffer_),
        [this, self](std::error_code ec, std::size_t length)
        {
            if (!ec)
            {
                parser_.feed(std::span<const uint8_t>(read_buffer_.data(), length));
                while (auto msg = parser_.next())
                {
                    on_message_(shared_from_this(), msg->first, msg->second);
                }
                do_read();
            }
            else
            {
                // »—œ–¿¬À≈ÕŒ: ÍÓÌ‚ÂÚ‡ˆËˇ ‚ UTF-8
                MCG_INFO("Session {} disconnected: {}", id_, mcg::util::error_message(ec));
            }
        });
}

void Session::do_write()
{
    auto self = shared_from_this();
    asio::async_write(socket_, asio::buffer(write_queue_.front()),
                      [this, self](std::error_code ec, std::size_t /*length*/)
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
                              MCG_ERROR("Session {} write error: {}", id_,
                                        mcg::util::error_message(ec));
                          }
                      });
}
} // namespace mcg::server