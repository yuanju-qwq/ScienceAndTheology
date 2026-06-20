#pragma once

// Cross-platform socket abstraction.
// Hides winsock2 vs POSIX differences behind a uniform API.

#if defined(_WIN32)
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0601  // Windows 7+
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mswsock.h>

    #pragma comment(lib, "ws2_32.lib")

    using socket_t = SOCKET;
    inline constexpr socket_t kInvalidSocket = INVALID_SOCKET;
    inline constexpr int kSocketError = SOCKET_ERROR;

    // On Windows, errno-style codes come from WSAGetLastError().
    inline int socket_errno() { return WSAGetLastError(); }

    // WSAEWOULDBLOCK on Windows ≈ EWOULDBLOCK on POSIX.
    inline bool would_block(int err) { return err == WSAEWOULDBLOCK; }
    inline bool conn_reset(int err) { return err == WSAECONNRESET || err == WSAECONNABORTED; }

    // Close a socket (Windows: closesocket).
    inline int close_socket(socket_t s) { return closesocket(s); }

    // Suppress unused-parameter warnings for MSVC extensions.
    #define SNT_SOCKLEN_T int

#else
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>

    using socket_t = int;
    inline constexpr socket_t kInvalidSocket = -1;
    inline constexpr int kSocketError = -1;

    inline int socket_errno() { return errno; }
    inline bool would_block(int err) { return err == EWOULDBLOCK || err == EAGAIN; }
    inline bool conn_reset(int err) { return err == ECONNRESET || err == EPIPE; }

    inline int close_socket(socket_t s) { return ::close(s); }

    #define SNT_SOCKLEN_T socklen_t
#endif

#include <string>
#include <cstdint>

namespace science_and_theology {
namespace server {

// RAII guard that initializes winsock on Windows (no-op on POSIX).
// Construct once per process; destructor cleans up.
class SocketLib {
public:
    SocketLib();
    ~SocketLib();
    SocketLib(const SocketLib&) = delete;
    SocketLib& operator=(const SocketLib&) = delete;

    bool is_initialized() const { return initialized_; }

private:
    bool initialized_ = false;
};

// Set a socket to non-blocking mode.
bool set_nonblocking(socket_t s);

// Set SO_REUSEADDR on a socket.
bool set_reuseaddr(socket_t s);

// Enable TCP_NODELAY (disable Nagle) on a socket.
bool set_tcp_nodelay(socket_t s);

// Convert a socket error code to a human-readable string.
std::string socket_error_string(int err);

} // namespace server
} // namespace science_and_theology
