// Sender: binds an ephemeral port, then pushes a numbered message through a
// rudp::Connection every 100 ms while polling for ack traffic coming back.
//
// Until the reliability layer is implemented, Connection::send() is a no-op,
// so the receiver will only see the single "raw ping" datagram sent directly
// through the socket (which proves the fully-implemented socket layer works
// end to end). Once you implement the TODOs, the numbered messages appear.

#include <rudp/connection.hpp>
#include <rudp/packet.hpp>
#include <rudp/socket.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <receiver-ip> <receiver-port>\n"
                  << "e.g.:  " << argv[0] << " 127.0.0.1 9000\n";
        return EXIT_FAILURE;
    }

    try {
        const auto port = static_cast<std::uint16_t>(std::stoi(argv[2]));
        const rudp::Endpoint remote = rudp::make_endpoint(argv[1], port);

        // Port 0 = OS picks an ephemeral local port. Nonblocking so the send
        // loop can poll for incoming acks without stalling.
        rudp::UdpSocket socket = rudp::UdpSocket::bind(0);
        socket.set_nonblocking();

        // Raw datagram straight through the socket, bypassing Connection —
        // lets you verify the transport works before any TODO is filled in.
        const std::string ping = "raw ping (socket layer works)";
        socket.send_to(std::as_bytes(std::span{ping.data(), ping.size()}), remote);
        std::cout << "sent raw ping to " << argv[1] << ":" << port << "\n";

        rudp::Connection connection(socket, remote);
        std::array<std::byte, rudp::kMaxDatagramSize> buffer{};

        for (int i = 0; i < 20; ++i) {
            const std::string message = "message " + std::to_string(i);
            connection.send(std::as_bytes(std::span{message.data(), message.size()}));
            std::cout << "send(\"" << message << "\")  [in flight: "
                      << connection.packets_in_flight() << "]\n";

            // Drain any datagrams the receiver sent back (acks, once
            // implemented) and feed them to the connection.
            rudp::Endpoint from{};
            while (auto received = socket.recv_from(buffer, from)) {
                connection.on_receive(std::span{buffer.data(), *received});
            }

            connection.update(rudp::Clock::now());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "done. packets still in flight (unacked): "
                  << connection.packets_in_flight() << "\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
