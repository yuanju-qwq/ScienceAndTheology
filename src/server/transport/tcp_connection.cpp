#include "tcp_connection.hpp"

#include <cstring>
#include <utility>

namespace science_and_theology {
namespace server {

namespace {

// Recv buffer chunk size per recv_to_buffer() call.
constexpr size_t kRecvChunkSize = 8192;

} // namespace

// ---------------------------------------------------------------------------
// TcpConnection
// ---------------------------------------------------------------------------

TcpConnection::TcpConnection(socket_t s) : sock_(s) {
    if (s != kInvalidSocket) {
        set_nonblocking(s);
        set_tcp_nodelay(s);
    }
}

TcpConnection::~TcpConnection() {
    close();
}

TcpConnection::TcpConnection(TcpConnection&& other) noexcept
    : sock_(other.sock_)
    , recv_buf_(std::move(other.recv_buf_))
    , peer_closed_(other.peer_closed_)
    , last_decode_error_(other.last_decode_error_) {
    other.sock_ = kInvalidSocket;
}

TcpConnection& TcpConnection::operator=(TcpConnection&& other) noexcept {
    if (this != &other) {
        close();
        sock_ = other.sock_;
        recv_buf_ = std::move(other.recv_buf_);
        peer_closed_ = other.peer_closed_;
        last_decode_error_ = other.last_decode_error_;
        other.sock_ = kInvalidSocket;
    }
    return *this;
}

int TcpConnection::recv_to_buffer() {
    if (sock_ == kInvalidSocket) return -2;

    uint8_t chunk[kRecvChunkSize];
    const int n = ::recv(sock_, reinterpret_cast<char*>(chunk),
                         static_cast<int>(kRecvChunkSize), 0);
    if (n > 0) {
        recv_buf_.insert(recv_buf_.end(), chunk, chunk + n);
        return n;
    }
    if (n == 0) {
        // Graceful close by peer.
        peer_closed_ = true;
        return 0;
    }
    // n < 0: error.
    const int err = socket_errno();
    if (would_block(err)) {
        return -1;
    }
    // Hard error.
    peer_closed_ = true;
    return -2;
}

bool TcpConnection::drain_frames(std::vector<Frame>& out) {
    while (!recv_buf_.empty()) {
        DecodeResult result = decode_frame(recv_buf_);
        switch (result.status) {
            case DecodeStatus::OK:
                if (result.frame) {
                    out.push_back(std::move(*result.frame));
                }
                break;
            case DecodeStatus::NEED_MORE_DATA:
            case DecodeStatus::INCOMPLETE:
                // Wait for more bytes.
                return true;
            default:
                // INVALID_MAGIC, INVALID_VERSION, PAYLOAD_TOO_LARGE, CRC_MISMATCH.
                last_decode_error_ = result.status;
                return false;
        }
    }
    return true;
}

int TcpConnection::send_bytes(const uint8_t* data, size_t len) {
    if (sock_ == kInvalidSocket) return -1;
    if (len == 0) return 0;
    const int n = ::send(sock_, reinterpret_cast<const char*>(data),
                         static_cast<int>(len), 0);
    return n;
}

int TcpConnection::send_bytes(const std::vector<uint8_t>& data) {
    return send_bytes(data.data(), data.size());
}

int TcpConnection::send_frame(PacketType type, uint64_t player_handle,
                              const uint8_t* payload, size_t payload_len,
                              FrameFlags flags) {
    auto wire = encode_frame(type, player_handle, payload, payload_len, flags);
    if (wire.empty() && payload_len > 0) return -1;
    return send_bytes(wire);
}

void TcpConnection::close() {
    if (sock_ != kInvalidSocket) {
        close_socket(sock_);
        sock_ = kInvalidSocket;
    }
    recv_buf_.clear();
}

void TcpConnection::shutdown_send() {
    if (sock_ != kInvalidSocket) {
#if defined(_WIN32)
        ::shutdown(sock_, SD_SEND);
#else
        ::shutdown(sock_, SHUT_WR);
#endif
    }
}

// ---------------------------------------------------------------------------
// TcpListener
// ---------------------------------------------------------------------------

TcpListener::~TcpListener() {
    close();
}

TcpListener::TcpListener(TcpListener&& other) noexcept
    : listen_sock_(other.listen_sock_) {
    other.listen_sock_ = kInvalidSocket;
}

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
    if (this != &other) {
        close();
        listen_sock_ = other.listen_sock_;
        other.listen_sock_ = kInvalidSocket;
    }
    return *this;
}

bool TcpListener::listen(uint16_t port, int backlog) {
    listen_sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock_ == kInvalidSocket) return false;

    set_reuseaddr(listen_sock_);
    set_nonblocking(listen_sock_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(listen_sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == kSocketError) {
        close();
        return false;
    }

    if (::listen(listen_sock_, backlog) == kSocketError) {
        close();
        return false;
    }

    return true;
}

std::optional<TcpConnection> TcpListener::accept() {
    if (listen_sock_ == kInvalidSocket) return std::nullopt;

    sockaddr_in client_addr{};
    SNT_SOCKLEN_T addr_len = sizeof(client_addr);
    socket_t client_sock = ::accept(listen_sock_,
                                    reinterpret_cast<sockaddr*>(&client_addr),
                                    &addr_len);
    if (client_sock == kInvalidSocket) {
        // No pending connection (would block) or error.
        return std::nullopt;
    }
    return TcpConnection(client_sock);
}

void TcpListener::close() {
    if (listen_sock_ != kInvalidSocket) {
        close_socket(listen_sock_);
        listen_sock_ = kInvalidSocket;
    }
}

} // namespace server
} // namespace science_and_theology
