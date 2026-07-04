#include <rudp/socket.hpp>

#include <stdexcept>
#include <system_error>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rudp {

namespace {

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

} // namespace

Endpoint make_endpoint(const char* ipv4, std::uint16_t port) {
    Endpoint ep{};
    ep.sin_family = AF_INET;
    ep.sin_port = htons(port); // ports live in network byte order too
    if (::inet_pton(AF_INET, ipv4, &ep.sin_addr) != 1) {
        throw std::invalid_argument(std::string("not a valid IPv4 address: ") + ipv4);
    }
    return ep;
}

UdpSocket::UdpSocket() : fd_(::socket(AF_INET, SOCK_DGRAM, 0)) {
    // INTERVIEW HOOK (RAII): acquisition happens here and nowhere else. If
    // it fails we throw, so no UdpSocket object ever exists without a live
    // fd behind it.
    if (fd_ < 0) {
        throw_errno("socket()");
    }
}

UdpSocket::~UdpSocket() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)) {}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            ::close(fd_); // release what we currently own before taking theirs
        }
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}

UdpSocket UdpSocket::bind(std::uint16_t port) {
    UdpSocket sock;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(sock.fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw_errno("bind()");
    }
    return sock; // moved out — ownership transfers to the caller
}

void UdpSocket::set_nonblocking(bool enabled) {
    const int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
        throw_errno("fcntl(F_GETFL)");
    }
    const int new_flags = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(fd_, F_SETFL, new_flags) < 0) {
        throw_errno("fcntl(F_SETFL)");
    }
}

std::size_t UdpSocket::send_to(std::span<const std::byte> data, const Endpoint& dest) {
    const ssize_t sent = ::sendto(fd_, data.data(), data.size(), 0,
                                  reinterpret_cast<const sockaddr*>(&dest),
                                  sizeof(dest));
    if (sent < 0) {
        throw_errno("sendto()");
    }
    return static_cast<std::size_t>(sent);
}

std::optional<std::size_t> UdpSocket::recv_from(std::span<std::byte> buffer, Endpoint& from) {
    socklen_t from_len = sizeof(from);
    const ssize_t received = ::recvfrom(fd_, buffer.data(), buffer.size(), 0,
                                        reinterpret_cast<sockaddr*>(&from),
                                        &from_len);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return std::nullopt; // nonblocking socket, nothing pending
        }
        throw_errno("recvfrom()");
    }
    return static_cast<std::size_t>(received);
}

} // namespace rudp
