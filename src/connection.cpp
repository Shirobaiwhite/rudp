#include <rudp/connection.hpp>

#include <rudp/packet.hpp>
#include <iostream>

// =============================================================================
// THE RELIABILITY LAYER — every body in this file is a deliberate stub.
// This is the part you implement. Suggested order (also in the README):
// serialize/deserialize first (src/packet.cpp), then sequence numbers, then
// acks, then retransmission.
// =============================================================================

namespace rudp {

bool sequence_greater_than(std::uint16_t a, std::uint16_t b) {
    // TODO(you): wrap-aware comparison — see the hint in connection.hpp.
    // Get this right FIRST; on_receive and process_ack both depend on it,
    // and it is the classic subtle bug in this whole exercise (everything
    // works for 65535 packets, then acks silently stop).
    return (a > b && a - b <= 32768) || (a < b && b - a > 32768);
}

Connection::Connection(UdpSocket& socket, Endpoint remote)
    : socket_(socket), remote_(remote) {}

void Connection::send(std::span<const std::byte> payload) {
    // TODO(you): send `payload` reliably. Steps:
    //
    //  1. Build a PacketHeader:
    //       protocol_id = kProtocolId
    //       sequence    = next_sequence_   (then increment next_sequence_;
    //                     uint16_t wraps 65535 -> 0 on its own, that's fine)
    //       ack         = remote_sequence_
    //       ack_bits    = received_bits_
    //     (Every data packet piggybacks our ack state — that is why this
    //     protocol needs no separate "ACK packet" type in steady state.)
    //
    std::uint32_t protocol_id = kProtocolId;
    std::uint16_t sequence = next_sequence_++;
    std::uint16_t ack = remote_sequence_;
    std::uint32_t ack_bits = received_bits_;

    PacketHeader h {
        .protocol_id = protocol_id,
        .sequence = sequence,
        .ack = ack,
        .ack_bits = ack_bits,
    };
    
    std::array<std::byte, kMaxDatagramSize> buf;
    const std::size_t n = serialize(h, payload, buf);
    if (n == 0) {
        return;
    }
    //  2. serialize() header + payload into a buffer of kMaxDatagramSize.
    //     If serialize returns 0, the payload was too big — drop or assert.
    //  3. socket_.send_to(...) the serialized bytes to remote_.
    //
    socket_.send_to(std::span<const std::byte>(buf.data(), n), remote_);
    //  4. Record it in pending_: sequence, a copy of the serialized
    //     datagram, last_sent = Clock::now(), send_count = 1. It stays
    //     there until process_ack() confirms the other side got it.
    pending_.push_back(
        PendingPacket {
            .sequence = h.sequence,
            .datagram = std::vector<std::byte>(buf.begin(), buf.begin() + n),
            .last_sent = Clock::now(),
            .send_count = 1,
        }
    );
    // (void)payload;
}

void Connection::on_receive(std::span<const std::byte> datagram) {
    // TODO(you): process one datagram from the socket. Steps:
    //
    //  1. deserialize() it. On std::nullopt, drop it silently — malformed
    //     and foreign packets are normal background noise on the internet,
    //     not an error worth logging per-packet.
    //
    std::span<const std::byte> payload;
    const auto h = deserialize(datagram, payload);
    if (!h) {
        return;
    }

    //  2. Update our record of what we've received (this feeds the ack
    //     fields of our NEXT outgoing packet):
    //       - if sequence_greater_than(header.sequence, remote_sequence_):
    //           shift received_bits_ left by the distance the sequence
    //           advanced (mark the old remote_sequence_'s bit as you shift),
    //           then set remote_sequence_ = header.sequence.
    //       - else: it's an older packet arriving late/out of order — set
    //           its bit in received_bits_. Bit N corresponds to sequence
    //           (remote_sequence_ - 1 - N). If it is older than 33 packets,
    //           or its bit is already set (duplicate), ignore it.
    //
    if (sequence_greater_than(h->sequence, remote_sequence_)) {
        std::uint16_t distance = h->sequence - remote_sequence_;
        if (distance > 32) {
            received_bits_ = 0;
        } else if (distance == 32) {
            received_bits_ = (1u << 31);
        } else {
            received_bits_ <<= distance;
            received_bits_ |= (1u << (distance - 1));
        }

        remote_sequence_ = h->sequence;
    } else {
        std::uint16_t distance = remote_sequence_ - h->sequence;
        if (distance == 0 || distance > 32) {
            return;
        }

        std::uint32_t bit = 1u << (distance - 1);

        if (received_bits_ & bit) {
            return;
        }

        received_bits_ |= bit;
    }
    //  3. process_ack(header.ack, header.ack_bits) — the peer just told us
    //     what THEY have received from us.
    process_ack(h->ack, h->ack_bits);
    //  4. Hand the payload to the application (for this project, printing
    //     it in the example is enough; a real SDK would invoke a callback
    //     or push to a queue).
    std::cout.write(reinterpret_cast<const char*>(payload.data()), payload.size()) << std::endl;
}

void Connection::update(Clock::time_point now) {
    // TODO(you): retransmit-on-timeout. Steps:
    //
    //  1. For each packet in pending_ where
    //     now - last_sent >= kRetransmitTimeout:
    //       - socket_.send_to() its stored datagram to remote_ again,
    //         unchanged — same bytes, same ORIGINAL sequence number, so the
    //         receiver can recognize a duplicate if the original was merely
    //         slow, not lost.
    //       - last_sent = now; ++send_count.
    //
    //  2. Optional hardening: if send_count exceeds some cap (say 10),
    //     consider the connection dead — drop the packet or surface a
    //     "disconnected" state instead of retrying forever.
    for (auto& packet : pending_) {
        if (now - packet.last_sent >= kRetransmitTimeout) {
            socket_.send_to(packet.datagram, remote_);
            packet.last_sent = now;
            ++packet.send_count;
        }
    }
    (void)now;
}

void Connection::process_ack(std::uint16_t ack, std::uint32_t ack_bits) {
    // TODO(you): remove everything the peer confirmed from pending_. The
    // header fields mean:
    //   ack       — the highest of OUR sequence numbers the peer received
    //   ack_bits  — bit N set => peer also received sequence (ack - 1 - N)
    //
    // So a pending packet with sequence S is confirmed when:
    //   - S == ack, or
    //   - sequence_greater_than(ack, S) and (ack - S - 1) < 32 and
    //     bit (ack - S - 1) of ack_bits is set
    //     (mind uint16_t wraparound when computing the distance!)
    //
    // Erase confirmed packets from pending_ (erase-remove / std::erase_if).
    // Anything left keeps aging toward retransmission in update().

    std::erase_if(pending_, [&](const PendingPacket& p) {
        if (p.sequence == ack) {
            return true;
        } else if (sequence_greater_than(ack, p.sequence)) {
            std::uint16_t distance = ack - p.sequence - 1;
            if (distance < 32 && (ack_bits & (1u << distance))) {
                return true;
            }
        }
        return false;
    });
    //
    // INTERVIEW HOOK (ack bitfields): this redundancy is the whole trick —
    // every packet re-acks the last 33, so any single lost ack is covered
    // by the next 32 packets. Reliability for the acks themselves, without
    // ever retransmitting an ack.
    (void)ack;
    (void)ack_bits;
}

} // namespace rudp
