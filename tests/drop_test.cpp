// drop_test: proves the simulated-loss + retransmit path works, in one
// process, deterministically, with no sleeping.
//
// Two Connections talk over loopback UDP. The test plays "lossy network"
// for the A->B direction: it reads A's datagrams off B's socket, discards
// 20% of them (fixed RNG seed, so every run is identical), and feeds the
// survivors to B. B sends one small packet back each round so its
// ack/ack_bits piggyback to A, per the protocol design. Retransmit
// timeouts are forced by handing update() a future `now` — it only
// compares timestamps, so a pretend clock is as good as a real wait.
//
// PASS criteria:
//   1. at least one datagram was dropped        (the drop mechanism engages)
//   2. every dropped sequence arrived again     (retransmission works)
//   3. A ends with zero packets in flight       (acks round-tripped)

#include <rudp/connection.hpp>
#include <rudp/packet.hpp>
#include <rudp/socket.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <span>
#include <string>

#include <arpa/inet.h>  // ntohs
#include <sys/socket.h> // getsockname

namespace {

// The sockets bind to 0.0.0.0 with an OS-picked port; recover the port and
// pair it with 127.0.0.1 so the two ends can address each other.
rudp::Endpoint loopback_endpoint_of(const rudp::UdpSocket& sock) {
    sockaddr_in bound{};
    socklen_t len = sizeof(bound);
    if (::getsockname(sock.native_handle(),
                      reinterpret_cast<sockaddr*>(&bound), &len) != 0) {
        std::perror("getsockname");
        std::exit(EXIT_FAILURE);
    }
    return rudp::make_endpoint("127.0.0.1", ntohs(bound.sin_port));
}

[[noreturn]] void fail(const std::string& why) {
    std::cout << "FAIL: " << why << std::endl;
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    using namespace std::chrono_literals;

    rudp::UdpSocket sock_a = rudp::UdpSocket::bind(0);
    rudp::UdpSocket sock_b = rudp::UdpSocket::bind(0);
    sock_a.set_nonblocking();
    sock_b.set_nonblocking();

    rudp::Connection a(sock_a, loopback_endpoint_of(sock_b));
    rudp::Connection b(sock_b, loopback_endpoint_of(sock_a));

    std::mt19937 rng{42}; // fixed seed: same drops every run, reproducible
    std::bernoulli_distribution drop(0.20);

    constexpr std::size_t kMessages = 20;
    for (std::size_t i = 0; i < kMessages; ++i) {
        const std::string msg = "message " + std::to_string(i);
        a.send(std::as_bytes(std::span{msg.data(), msg.size()}));
    }
    if (a.packets_in_flight() != kMessages)
        fail("send() did not record " + std::to_string(kMessages) + " pending packets");

    int dropped_total = 0;
    std::set<std::uint16_t> delivered;           // seqs B has actually received
    std::set<std::uint16_t> awaiting_retransmit; // dropped seqs B still lacks

    std::array<std::byte, rudp::kMaxDatagramSize> buffer{};
    rudp::Endpoint from{};

    constexpr int kMaxRounds = 50;
    int round = 0;
    for (; round < kMaxRounds && a.packets_in_flight() > 0; ++round) {
        // Lossy A->B hop: peek at each datagram's header, then roll the dice.
        while (auto n = sock_b.recv_from(buffer, from)) {
            std::span<const std::byte> payload;
            const auto header = rudp::deserialize(
                std::span<const std::byte>{buffer.data(), *n}, payload);
            if (!header) fail("A sent a datagram that does not deserialize");

            if (drop(rng)) {
                ++dropped_total;
                // Only a drop of something B still lacks demands a
                // retransmit. Dropping a duplicate (A resent before the
                // ack got back) is harmless and must not fail the test.
                if (!delivered.contains(header->sequence))
                    awaiting_retransmit.insert(header->sequence);
                std::cout << "round " << round << ": DROPPED seq "
                          << header->sequence << std::endl;
                continue; // B never sees it
            }
            delivered.insert(header->sequence);
            awaiting_retransmit.erase(header->sequence); // (re)arrival
            b.on_receive(std::span{buffer.data(), *n});
        }

        // B sends one small packet so its ack state piggybacks back to A.
        const std::string carrier = "ack carrier";
        b.send(std::as_bytes(std::span{carrier.data(), carrier.size()}));

        // Lossless B->A hop.
        while (auto n = sock_a.recv_from(buffer, from)) {
            a.on_receive(std::span{buffer.data(), *n});
        }

        // Force the 250 ms retransmit timeout without sleeping: each round
        // pretends another 300 ms have passed.
        a.update(rudp::Clock::now() + (round + 1) * 300ms);

        std::cout << "round " << round << ": in flight = "
                  << a.packets_in_flight() << std::endl;
    }

    std::cout << "----\ndropped " << dropped_total << " datagram(s) over "
              << round << " round(s)" << std::endl;

    if (dropped_total == 0)
        fail("nothing was dropped — drop simulation never engaged");
    if (!awaiting_retransmit.empty())
        fail(std::to_string(awaiting_retransmit.size()) +
             " dropped sequence(s) were never retransmitted");
    if (a.packets_in_flight() != 0)
        fail("A still has " + std::to_string(a.packets_in_flight()) +
             " unacked packet(s) after " + std::to_string(round) + " rounds");

    std::cout << "PASS" << std::endl;
    return EXIT_SUCCESS;
}
