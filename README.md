# reliable-udp

UDP is fast but makes no delivery guarantees; TCP guarantees delivery but adds latency and head-of-line blocking. This library adds **configurable** reliability on top of UDP — so you get guaranteed delivery only where you need it, without paying TCP's cost on the traffic where you don't.

> **Scope:** this is a from-scratch **educational / reference implementation** for understanding game-networking internals — built to be read, stepped through, and broken on purpose. It is **not** production infrastructure: no congestion control, no encryption, no connection handshake, IPv4/POSIX only.

## Problems it solves

Ranked most to least important:

1. **Reliable delivery over an unreliable transport** — sequence numbers, acknowledgements, and retransmission on top of raw datagrams.
2. **Low latency for real-time traffic** — no head-of-line blocking; a lost packet never stalls the packets behind it.
3. **Selective per-message guarantees** — reliable where it matters (chat, state changes), fire-and-forget where it doesn't (position updates that are stale 50 ms later anyway).
4. **A clean RAII-based SDK/library boundary** — `UdpSocket` owns the platform resource; everything above it is portable. Swapping POSIX for Winsock or a console networking API touches one file.
5. **Cross-platform-safe wire format** — fixed-width fields, explicit network byte order, no struct-memcpy: both ends parse the same 12 bytes the same way regardless of architecture.
6. **Wireshark-observable protocol** — a plaintext header with a magic number you can filter on, so every sequence, ack, and retransmit is visible on the wire.

## Prior art / see also

[ENet](https://github.com/lsalzman/enet), [Valve GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets), [KCP](https://github.com/skywind3000/kcp), and [QUIC](https://datatracker.ietf.org/doc/html/rfc9000) all solve this same problem for real. This project is a small educational cousin of those — for production you'd adopt one of them rather than roll your own.

## Build & run

```sh
cmake -S . -B build
cmake --build build
```

Then in one terminal:

```sh
./build/receiver 9000
```

and in another:

```sh
./build/sender 127.0.0.1 9000
```

The receiver prints every datagram that arrives. Until you implement the reliability layer you'll only see the sender's raw ping (the `Connection` methods are stubs); once implemented, the 20 numbered messages — and the acks flowing back — appear too.

## Suggested implementation order

The reliability logic is deliberately left as `TODO(you)` stubs in `src/packet.cpp` and `src/connection.cpp`, with step-by-step guidance in the comments. Build it in this order — each step is testable on its own:

1. **`serialize` / `deserialize`** (`src/packet.cpp`) — get bytes on and off the wire correctly. Testable without any networking at all: round-trip a header through both functions.
2. **Sequence numbers only** — stamp outgoing packets in `Connection::send`, track the highest seen in `on_receive`. Watch the numbers count up in Wireshark.
3. **Acks + ack_bits** — piggyback what you've received onto every outgoing packet; implement `process_ack` so the sender's pending list drains.
4. **Retransmit on timeout** — `Connection::update` resends anything unacked past the timeout.
5. **Inject artificial packet loss** (e.g. randomly skip the `send_to` in `Connection::send` 20% of the time) and watch the recovery happen in Wireshark: gap in sequence, missing bit in ack_bits, retransmit with the original sequence number.

## Keep a decisions log

Keep a `NOTES.md` as you implement: every design choice (why 32 ack bits? why not resend immediately on a gap?) and every bug you hit (sequence wrap-around is the classic) becomes an interview talking point. The log is half the value of the exercise.
