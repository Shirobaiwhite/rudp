// Receiver: binds the given port and blocks on recv, printing every datagram
// that arrives and feeding it into a rudp::Connection.
//
// Until the reliability layer is implemented, on_receive() is a no-op, so
// this just reports raw datagram arrivals (sender's raw ping shows up
// immediately). Once you implement the TODOs, this side will start decoding
// headers and — because update() sends piggybacked acks — acking them.
// Run it, then point the sender at it. Ctrl-C to stop.

#include <rudp/connection.hpp>
#include <rudp/packet.hpp>
#include <rudp/socket.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>

#include <arpa/inet.h> // inet_ntop, ntohs (for printing the sender address)
#include <random>

namespace {

std::string to_string(const rudp::Endpoint& ep) {
    char ip[INET_ADDRSTRLEN] = {};
    ::inet_ntop(AF_INET, &ep.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(ep.sin_port));
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <port>\n"
                  << "e.g.:  " << argv[0] << " 9000\n";
        return EXIT_FAILURE;
    }

    try {
        const auto port = static_cast<std::uint16_t>(std::stoi(argv[1]));
        rudp::UdpSocket socket = rudp::UdpSocket::bind(port);
        // std::endl (not "\n") throughout: flush per line so output is
        // visible immediately even when piped or redirected to a file.
        std::cout << "listening on port " << port << " (Ctrl-C to stop)" << std::endl;

        // A Connection is bound to one remote endpoint, and we don't know
        // the sender's address until its first datagram arrives — so the
        // connection is created lazily. (A real server would keep a map of
        // endpoint -> Connection, one per client.)
        std::optional<rudp::Connection> connection;

        std::array<std::byte, rudp::kMaxDatagramSize> buffer{};
        rudp::Endpoint from{};

        std::mt19937 rng{std::random_device{}()};
        std::bernoulli_distribution drop(0.20);

        while (true) {
            // Blocking socket: recv_from always returns a value here.
            const std::size_t received = socket.recv_from(buffer, from).value();

            if (drop(rng)) {
                std::cout << "  [simulated loss: dropped " << received
                        << "-byte datagram]" << std::endl;
                continue;  // pretend it never arrived
            }

            std::cout << "datagram from " << to_string(from) << ": "
                      << received << " bytes" << std::endl;

            if (!connection) {
                connection.emplace(socket, from);
                std::cout << "created connection for " << to_string(from) << std::endl;
            }

            connection->on_receive(std::span{buffer.data(), received});
            connection->update(rudp::Clock::now());
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
