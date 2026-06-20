#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "socket_platform.hpp"
#include "../protocol/protocol_codec.hpp"

namespace science_and_theology {
namespace server {

// A connected TCP socket with a built-in receive buffer and frame decoder.
//
// Usage (server side, after accept):
//   TcpConnection conn(accepted_socket);
//   conn.recv_to_buffer();          // pull bytes from kernel
//   auto frames = conn.drain_frames(); // decode complete frames
//   conn.send_bytes(wire_data);     // send encoded frames
//
// All operations are non-blocking. The caller is responsible for polling
// (e.g. via select() / WSAPoll) to know when data is available.
class TcpConnection {
public:
    TcpConnection() = default;
    explicit TcpConnection(socket_t s);
    ~TcpConnection();

    // Non-copyable, movable.
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
    TcpConnection(TcpConnection&& other) noexcept;
    TcpConnection& operator=(TcpConnection&& other) noexcept;

    bool is_valid() const { return sock_ != kInvalidSocket; }
    socket_t raw_socket() const { return sock_; }

    // Read available bytes from the kernel into the internal recv buffer.
    // Returns:
    //   >0  — number of bytes appended
    //   0   — connection closed by peer (graceful)
    //   -1  — would block (no data available right now)
    //   -2  — connection error (caller should close)
    int recv_to_buffer();

    // Decode all complete frames currently in the recv buffer.
    // On decode errors (bad magic / crc), returns false and the connection
    // should be considered corrupt — caller should close it.
    // Successfully decoded frames are appended to `out`.
    bool drain_frames(std::vector<Frame>& out);

    // Send raw bytes. Returns number of bytes sent, or -1 on error.
    // For non-blocking sockets, a partial send is possible; the caller
    // should retry the unsent portion.
    int send_bytes(const uint8_t* data, size_t len);
    int send_bytes(const std::vector<uint8_t>& data);

    // Send a single encoded frame (convenience).
    int send_frame(PacketType type, uint64_t player_id,
                   const uint8_t* payload, size_t payload_len,
                   FrameFlags flags = FrameFlags::NONE);

    // Close the socket.
    void close();

    // Gracefully shut down the send side (shutdown(SHUT_WR)).
    // Pending data in the send buffer is still delivered to the peer.
    // The socket must still be close()'d afterwards.
    void shutdown_send();

    // Returns true if the peer has disconnected (recv returned 0).
    bool peer_closed() const { return peer_closed_; }

    // Returns any decode error that occurred.
    DecodeStatus last_decode_error() const { return last_decode_error_; }

private:
    socket_t sock_ = kInvalidSocket;
    std::vector<uint8_t> recv_buf_;
    bool peer_closed_ = false;
    DecodeStatus last_decode_error_ = DecodeStatus::OK;
};

// A listening TCP socket that accepts inbound connections.
class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();

    // Non-copyable, movable.
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;
    TcpListener(TcpListener&& other) noexcept;
    TcpListener& operator=(TcpListener&& other) noexcept;

    // Bind and listen on the given port (all interfaces).
    // Returns true on success.
    bool listen(uint16_t port, int backlog = 16);

    // Accept one pending connection (non-blocking).
    // Returns a valid TcpConnection if a client connected, or nullopt
    // if no connection is pending.
    std::optional<TcpConnection> accept();

    bool is_valid() const { return listen_sock_ != kInvalidSocket; }
    socket_t raw_socket() const { return listen_sock_; }

    void close();

private:
    socket_t listen_sock_ = kInvalidSocket;
};

} // namespace server
} // namespace science_and_theology
