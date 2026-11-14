#include "shared/util/error.h"

namespace mcg::util
{

std::string error_message(const std::error_code& ec)
{
    if (!ec)
    {
        return "no error";
    }

    switch (ec.value())
    {
    case 2: // Asio EOF
        return "Connection closed gracefully by peer (EOF)";
    case 98:    // EADDRINUSE (Linux)
    case 10048: // WSAEADDRINUSE (Windows)
        return "Address already in use (port is busy)";
    case 104:   // ECONNRESET (Linux)
    case 10054: // WSAECONNRESET (Windows)
        return "Connection reset by peer (client crashed or closed abruptly)";
    case 103:   // ECONNABORTED (Linux)
    case 10053: // WSAECONNABORTED (Windows)
        return "Connection aborted";
    case 111:   // ECONNREFUSED (Linux)
    case 10061: // WSAECONNREFUSED (Windows)
        return "Connection refused (server is offline)";
    case 110:   // ETIMEDOUT (Linux)
    case 10060: // WSAETIMEDOUT (Windows)
        return "Connection timed out";
    case 125: // ECANCELED (Linux)
        return "Operation canceled (socket closed)";
    default:
        return "Network error (code: " + std::to_string(ec.value()) + ")";
    }
}

} // namespace mcg::util