#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "socket_platform.hpp"

namespace science_and_theology {
namespace server {

// A received UDP datagram with sender address.
struct UdpDatagram {
    std::vector<uint8_t> data;
    std::string from_ip;   // dotted-decimal, e.g. "192.168.1.5"
    uint16_t from_port = 0;
};

// A bound UDP socket for unreliable datagram transport.
//
// Used for two channels (design §4 方案B):
//   1. High-frequency player position updates (POS_UPDATE, 20Hz).
//   2. LAN discovery broadcast (DISCOVER_REQ / DISCOVER_REPLY).
//
// All operations are non-blocking.
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();

    // Non-copyable, movable.
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // Bind to a specific port (for receiving). If port is 0, the OS
    // assigns an ephemeral port (useful for send-only sockets).
    bool bind(uint16_t port);

    // Enable broadcast mode (SO_BROADCAST) for LAN discovery.
    bool enable_broadcast();

    // Send a datagram to a specific destination.
    bool send_to(const std::string& ip, uint16_t port,
                 const uint8_t* data, size_t len);
    bool send_to(const std::string& ip, uint16_t port,
                 const std::vector<uint8_t>& data);

    // Broadcast a datagram to the given port on all interfaces.
    bool broadcast(uint16_t port, const uint8_t* data, size_t len);
    bool broadcast(uint16_t port, const std::vector<uint8_t>& data);

    // Receive one datagram (non-blocking). Returns nullopt if no data.
    std::optional<UdpDatagram> recv_from();

    bool is_valid() const { return sock_ != kInvalidSocket; }
    socket_t raw_socket() const { return sock_; }

    void close();

private:
    socket_t sock_ = kInvalidSocket;
};

} // namespace server
} // namespace science_and_theology
