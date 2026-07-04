#pragma once

// PLATFORM NOTE: This wrapper is deliberately POSIX-only (BSD sockets on
// Linux/macOS). In a real console SDK you would keep this exact public
// interface and swap the implementation per platform (Winsock on Windows,
// the per-console networking APIs on PlayStation/Switch/Xbox). That
// abstraction boundary — one portable interface, N platform backends —
// IS the SDK.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <netinet/in.h> // sockaddr_in

namespace rudp {

// For this learning project we use sockaddr_in directly. A production SDK
// would wrap this in its own address type so the public header never leaks
// platform headers.
using Endpoint = ::sockaddr_in;

// Build an IPv4 endpoint from a dotted-quad string ("127.0.0.1") and a port.
// Throws std::invalid_argument if the address string is malformed.
Endpoint make_endpoint(const char* ipv4, std::uint16_t port);

// INTERVIEW HOOK (RAII): the socket file descriptor is a resource with a
// lifetime. Acquiring it in the constructor and releasing it in the
// destructor means it is impossible to leak on any exit path — early
// return, exception, whatever. No close() calls sprinkled through
// application code.
class UdpSocket {
public:
    // Acquires an AF_INET/SOCK_DGRAM socket. Throws std::system_error on
    // failure — an object that constructs successfully always owns a
    // valid fd (no half-alive states to check for).
    UdpSocket();

    ~UdpSocket();

    // INTERVIEW HOOK (move-only ownership): exactly one object owns the fd
    // at any time. Copying is deleted (two owners would double-close);
    // moving transfers the fd and nulls the source so its destructor
    // becomes a no-op. Same pattern as std::unique_ptr.
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // Factory: create a socket and bind it to a local port (0 = let the OS
    // pick an ephemeral port). Throws std::system_error on failure.
    static UdpSocket bind(std::uint16_t port);

    // Toggle O_NONBLOCK. When nonblocking, recv_from returns std::nullopt
    // instead of blocking when no datagram is pending.
    void set_nonblocking(bool enabled = true);

    // Send one datagram to `dest`. Returns bytes sent.
    // Throws std::system_error on failure.
    std::size_t send_to(std::span<const std::byte> data, const Endpoint& dest);

    // Receive one datagram into `buffer`; fills `from` with the sender's
    // address. Returns the datagram size, or std::nullopt if the socket is
    // nonblocking and nothing is pending. Throws std::system_error on
    // other failures.
    std::optional<std::size_t> recv_from(std::span<std::byte> buffer, Endpoint& from);

    int native_handle() const noexcept { return fd_; }

private:
    int fd_ = -1;
};

} // namespace rudp
