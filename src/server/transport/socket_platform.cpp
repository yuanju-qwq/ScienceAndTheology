#include "socket_platform.hpp"

#include <cstring>

namespace science_and_theology {
namespace server {

#if defined(_WIN32)

SocketLib::SocketLib() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    initialized_ = (result == 0);
}

SocketLib::~SocketLib() {
    if (initialized_) {
        WSACleanup();
    }
}

bool set_nonblocking(socket_t s) {
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

bool set_reuseaddr(socket_t s) {
    BOOL opt = TRUE;
    return setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                      reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

bool set_tcp_nodelay(socket_t s) {
    BOOL opt = TRUE;
    return setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
                      reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

std::string socket_error_string(int err) {
    char* s = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<LPSTR>(&s), 0, nullptr);
    std::string result;
    if (s) {
        result = s;
        LocalFree(s);
        // Trim trailing whitespace/newlines.
        while (!result.empty() &&
               (result.back() == '\r' || result.back() == '\n' ||
                result.back() == ' ')) {
            result.pop_back();
        }
    } else {
        result = "WSA error " + std::to_string(err);
    }
    return result;
}

#else // POSIX

SocketLib::SocketLib() : initialized_(true) {}
SocketLib::~SocketLib() {}

bool set_nonblocking(socket_t s) {
    int flags = fcntl(s, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool set_reuseaddr(socket_t s) {
    int opt = 1;
    return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0;
}

bool set_tcp_nodelay(socket_t s) {
    int opt = 1;
    return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == 0;
}

std::string socket_error_string(int err) {
    return std::string(strerror(err));
}

#endif

} // namespace server
} // namespace science_and_theology
