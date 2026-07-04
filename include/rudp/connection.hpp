#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <rudp/socket.hpp>

namespace rudp {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// TODO(you): implement in src/connection.cpp.
//
// Wrap-aware sequence comparison: is `a` "newer than" `b`, given that
// uint16_t sequence numbers wrap 65535 -> 0? A naive `a > b` breaks at the
// wrap (0 should count as newer than 65535).
//
// INTERVIEW HOOK (sequence wrap-around): the classic trick is to treat the
// sequence space as a circle and say a is newer if it is less than half the
// space "ahead" of b:
//   (a > b && a - b <= 32768) || (a < b && b - a > 32768)
// Related: RFC 1982 "Serial Number Arithmetic".
// ---------------------------------------------------------------------------
bool sequence_greater_than(std::uint16_t a, std::uint16_t b);

// One reliable "virtual connection" to a single remote endpoint, multiplexed
// over a shared (unconnected) UDP socket. Owns no OS resources itself — the
// socket is borrowed, so the socket must outlive the Connection.
//
// The reliability scheme this class is meant to implement (all TODO — see
// src/connection.cpp for per-method guidance):
//   - every outgoing packet is stamped with an incrementing sequence number
//   - every outgoing packet also carries ack + ack_bits describing what we
//     have received, so acks piggyback on normal traffic
//   - sent packets are kept until acked; update() retransmits any packet
//     that has gone unacked past a timeout
class Connection {
public:
    Connection(UdpSocket& socket, Endpoint remote);

    // How long a packet may go unacked before update() resends it. Kept as
    // a fixed constant for simplicity; real stacks derive this from a
    // smoothed RTT estimate (see e.g. TCP's RTO calculation).
    static constexpr auto kRetransmitTimeout = std::chrono::milliseconds(250);

    // TODO(you): send `payload` reliably. Stub — currently a no-op.
    void send(std::span<const std::byte> payload);

    // TODO(you): process one raw datagram received from the socket.
    // Stub — currently a no-op.
    void on_receive(std::span<const std::byte> datagram);

    // TODO(you): drive time-based work (retransmits). Call this every tick.
    // Stub — currently a no-op.
    void update(Clock::time_point now);

    // Observability so the examples can print progress once implemented.
    std::size_t packets_in_flight() const noexcept { return pending_.size(); }
    std::uint16_t local_sequence() const noexcept { return next_sequence_; }
    std::uint16_t remote_sequence() const noexcept { return remote_sequence_; }

private:
    // TODO(you): apply the ack + ack_bits fields from a received header to
    // the pending_ list. Stub.
    void process_ack(std::uint16_t ack, std::uint32_t ack_bits);

    // A sent-but-not-yet-acked packet, kept around so it can be resent.
    // We store the fully serialized datagram so a retransmit is just
    // send_to() again — no need to re-serialize (the resent packet keeps
    // its ORIGINAL sequence number; that is what lets the receiver detect
    // the duplicate).
    struct PendingPacket {
        std::uint16_t sequence;
        std::vector<std::byte> datagram;
        Clock::time_point last_sent;
        int send_count = 0;
    };

    UdpSocket& socket_;
    Endpoint remote_;

    std::uint16_t next_sequence_ = 1;    // sequence for the next packet we send
    std::uint16_t remote_sequence_ = 0;  // highest sequence seen from the peer
    std::uint32_t received_bits_ = 0;    // INTERVIEW HOOK (ack bitfields):
                                         // bit N set => we received packet
                                         // (remote_sequence_ - 1 - N).
                                         // Shifted as remote_sequence_
                                         // advances. This is what we echo
                                         // back as ack_bits.

    std::vector<PendingPacket> pending_; // sent, awaiting ack
};

} // namespace rudp
