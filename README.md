# microTCP

A reliable transport protocol implemented from scratch on top of UDP,
written for the Computer Networks course (CS-335a, University of Crete).

The skeleton (headers, build system, test harness) was provided by the
course staff; `lib/microtcp.c` — the actual protocol implementation — is
our work.

## What's implemented

- Three-way handshake (`SYN` / `SYN-ACK` / `ACK`) for connection setup
- Reliable data transfer with per-packet CRC-32 checksums
- Sliding window flow control
- Congestion control (slow start / congestion avoidance via `cwnd` and
  `ssthresh`)
- Four-way connection teardown (`FIN` / `ACK` in both directions)

## Build

```bash
mkdir build && cd build
cmake ..
make
```

This produces the `microtcp` library and the test binaries under `test/`
(`test_microtcp_client`, `test_microtcp_server`, `bandwidth_test`,
`traffic_generator`, `traffic_generator_client`).

## Running the tests

Start a server and connect to it with the matching client, e.g.:

```bash
./test/test_microtcp_server <port>
./test/test_microtcp_client <server_ip> <port>
```

`bandwidth_test` and `traffic_generator` exercise throughput and loss
behavior under the implemented congestion control.

## Layout

```
lib/      microtcp.c / microtcp.h — the protocol implementation
test/     course-provided test harness and client/server programs
utils/    CRC-32 checksum helper, logging macros
```

## Authors

Giorgos Mathioudakis, Nikos Iliakis
