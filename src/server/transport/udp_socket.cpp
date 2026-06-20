#include "udp_socket.hpp"

#include <cstring>
#include <utility>

namespace science_and_theology {
namespace server {

UdpSocket::~UdpSocket() {
    close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : sock_(other.sock_) {
    other.sock_ = kInvalidSocket;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        sock_ = other.sock_;
        other.sock_ = kInvalidSocket;
    }
    return *this;
}

bool UdpSocket::bind(uint16_t port) {
    sock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == kInvalidSocket) return false;

    set_reuseaddr(sock_);
    set_nonblocking(sock_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == kSocketError) {
        close();
        return false;
    }
    return true;
}

bool UdpSocket::enable_broadcast() {
    if (sock_ == kInvalidSocket) return false;
    int opt = 1;
    return setsockopt(sock_, SOL_SOCKET, SO_BROADCAST,
                      reinterpret_cast<const char*>(&opt), sizeof(opt)) == 0;
}

bool UdpSocket::send_to(const std::string& ip, uint16_t port,
                        const uint8_t* data, size_t len) {
    if (sock_ == kInvalidSocket) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        return false;
    }

    const int n = ::sendto(sock_, reinterpret_cast<const char*>(data),
                           static_cast<int>(len), 0,
                           reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return n != kSocketError;
}

bool UdpSocket::send_to(const std::string& ip, uint16_t port,
                        const std::vector<uint8_t>& data) {
    return send_to(ip, port, data.data(), data.size());
}

bool UdpSocket::broadcast(uint16_t port, const uint8_t* data, size_t len) {
    // 255.255.255.255 reaches all hosts on the local subnet.
    return send_to("255.255.255.255", port, data, len);
}

bool UdpSocket::broadcast(uint16_t port, const std::vector<uint8_t>& data) {
    return broadcast(port, data.data(), data.size());
}

std::optional<UdpDatagram> UdpSocket::recv_from() {
    if (sock_ == kInvalidSocket) return std::nullopt;

    uint8_t buf[65536];
    sockaddr_in from_addr{};
    SNT_SOCKLEN_T from_len = sizeof(from_addr);

    const int n = ::recvfrom(sock_, reinterpret_cast<char*>(buf),
                             sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&from_addr),
                             &from_len);
    if (n <= 0) {
        return std::nullopt;
    }

    UdpDatagram gram;
    gram.data.assign(buf, buf + n);
    // inet_ntop is thread-safe (unlike inet_ntoa).
    char ip_str[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
    gram.from_ip = ip_str;
    gram.from_port = ntohs(from_addr.sin_port);
    return gram;
}

void UdpSocket::close() {
    if (sock_ != kInvalidSocket) {
        close_socket(sock_);
        sock_ = kInvalidSocket;
    }
}

} // namespace server
} // namespace science_and_theology
