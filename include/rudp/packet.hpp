#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace rudp {

// INTERVIEW HOOK (protocol versioning / backward compat): the magic value
// doubles as a version gate. Spells "RUD1". If the wire format ever changes,
// bump this ("RUD2") and old clients simply reject new packets instead of
// misparsing them. Cheap insurance for live games where you cannot force
// every player to patch on the same day.
inline constexpr std::uint32_t kProtocolId = 0x52554431; // "RUD1"

// Wire layout — exactly 12 bytes, in this order:
//
//   offset  size  field
//   ------  ----  -----------------------------------------------------
//        0     4  protocol_id  magic; reject anything that doesn't match
//        4     2  sequence     this packet's sequence number
//        6     2  ack          highest sequence received from the peer
//        8     4  ack_bits     bitfield: bit N set => (ack - 1 - N) also
//                              received. One packet acks up to 33 packets,
//                              so acks survive being lost themselves.
//
// INTERVIEW HOOK (fixed-width types): the header uses only <cstdint>
// fixed-width types. `int` or `long` vary in size across
// platforms/compilers; a header defined with them would be a different
// number of bytes on different machines and the two ends would silently
// misparse each other. Fixed-width types pin the layout everywhere.
//
// INTERVIEW HOOK (endianness): size alone isn't enough — a little-endian
// x86 PC and a big-endian peer disagree on byte ORDER within each field.
// Multi-byte fields must be converted to network byte order (big-endian)
// on the way out (htonl/htons) and back to host order on the way in
// (ntohl/ntohs). This also means serialization is explicit field-by-field
// writes — never memcpy the whole struct (that would also bake in compiler
// padding decisions).
struct PacketHeader {
    std::uint32_t protocol_id;
    std::uint16_t sequence;
    std::uint16_t ack;
    std::uint32_t ack_bits;
};

inline constexpr std::size_t kHeaderSize = 12;

// Keep header + payload comfortably under a typical internet-path MTU
// (~1500 minus IP/UDP overhead and a safety margin) so datagrams are not
// fragmented — a fragmented datagram is lost if ANY fragment is lost.
inline constexpr std::size_t kMaxPayloadSize = 1200;
inline constexpr std::size_t kMaxDatagramSize = kHeaderSize + kMaxPayloadSize;

// ---------------------------------------------------------------------------
// TODO(you): implement both of these in src/packet.cpp. Step-by-step guidance
// is in the stub bodies there.
// ---------------------------------------------------------------------------

// Serialize `header` followed by `payload` into `out`.
// Returns the total number of bytes written (kHeaderSize + payload.size()),
// or 0 if `out` is too small or `payload` exceeds kMaxPayloadSize.
std::size_t serialize(const PacketHeader& header,
                      std::span<const std::byte> payload,
                      std::span<std::byte> out);

// Parse one received datagram. On success returns the header (converted to
// host byte order) and sets `payload` to view the payload bytes *inside*
// `datagram` (no copy — so `datagram` must outlive `payload`).
// Returns std::nullopt for anything malformed or foreign: too short to hold
// a header, or protocol_id mismatch.
std::optional<PacketHeader> deserialize(std::span<const std::byte> datagram,
                                        std::span<const std::byte>& payload);

} // namespace rudp
