#include <rudp/packet.hpp>

// You will want these for the byte-order conversions:
#include <arpa/inet.h>   // htonl, htons, ntohl, ntohs
#include <cstring>

namespace rudp {

std::size_t serialize(const PacketHeader& header,
                      std::span<const std::byte> payload,
                      std::span<std::byte> out) {
    // TODO(you): implement. Suggested steps:
    //
    //  1. Validate: payload.size() <= kMaxPayloadSize, and
    //     out.size() >= kHeaderSize + payload.size(). Return 0 otherwise.
    if (payload.size() > kMaxPayloadSize || out.size() < kHeaderSize + payload.size()) {
        return 0;
    }

    //  2. Convert each multi-byte field to network byte order (big-endian):
    //       htonl(header.protocol_id), htons(header.sequence),
    //       htons(header.ack), htonl(header.ack_bits).
    //
    std::uint32_t pid = htonl(header.protocol_id);
    std::uint16_t seq = htons(header.sequence);
    std::uint16_t ack = htons(header.ack);
    std::uint32_t ack_bits = htonl(header.ack_bits);
    //  3. Copy the converted fields into `out` at the offsets documented in
    //     packet.hpp (0, 4, 6, 8). std::memcpy from each converted value is
    //     the safe way — it has no alignment requirements. Do NOT memcpy the
    //     whole PacketHeader struct: that would write host byte order and
    //     whatever padding the compiler chose.
    //
    std::memcpy(out.data() + 0, &pid, sizeof(pid));
    std::memcpy(out.data() + 4, &seq, sizeof(seq));
    std::memcpy(out.data() + 6, &ack, sizeof(ack));
    std::memcpy(out.data() + 8, &ack_bits, sizeof(ack_bits));

    std::memcpy(out.data() + kHeaderSize, payload.data(), payload.size());

    return kHeaderSize + payload.size();
    //  4. Copy `payload` in right after the header (offset kHeaderSize).
    //
    //  5. Return kHeaderSize + payload.size().
    (void)header;
    (void)payload;
    (void)out;
    return 0; // stub
}

std::optional<PacketHeader> deserialize(std::span<const std::byte> datagram,
                                        std::span<const std::byte>& payload) {
    // TODO(you): implement. Suggested steps:
    //
    //  1. If datagram.size() < kHeaderSize, return std::nullopt — a UDP
    //     socket will happily hand you truncated garbage from port
    //     scanners and misdirected traffic; never index first, check first.
    //
    if (datagram.size() < kHeaderSize) {
        return std::nullopt;
    }

    //  2. std::memcpy each field out of `datagram` at offsets 0, 4, 6, 8,
    //     then convert from network to host byte order (ntohl/ntohs) —
    //     the mirror image of serialize().
    //
    std::uint32_t pid_net;
    std::memcpy(&pid_net, datagram.data() + 0, sizeof(pid_net));
    
    if (kProtocolId != ntohl(pid_net)) {
        return std::nullopt;
    }
    
    std::uint16_t seq_net;
    std::uint16_t ack_net;
    std::uint32_t ack_bits_net;

    std::memcpy(&seq_net, datagram.data() + 4, sizeof(seq_net));
    std::memcpy(&ack_net, datagram.data() + 6, sizeof(ack_net));
    std::memcpy(&ack_bits_net, datagram.data() + 8, sizeof(ack_bits_net));
    //  3. If protocol_id != kProtocolId, return std::nullopt. This one
    //     check rejects foreign packets AND peers speaking an incompatible
    //     protocol version (see the versioning note in packet.hpp).
    //
    PacketHeader h {
        .protocol_id = ntohl(pid_net),
        .sequence = ntohs(seq_net),
        .ack = ntohs(ack_net),
        .ack_bits = ntohl(ack_bits_net),
    };


    //  4. Point `payload` at the rest: datagram.subspan(kHeaderSize).
    //     (A subspan is a view into the caller's buffer — zero-copy.)
    //
    payload = datagram.subspan(kHeaderSize);

    return h;

    //  5. Return the header.
}

} // namespace rudp
