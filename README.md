# RTMP TimeShift Proxy

Native Qt 6 desktop multi-stream relay for Windows 10/11 x64 and Linux x86_64.
It accepts concurrent OBS H.264/AAC contributions over RTMP or encrypted SRT
through one managed MediaMTX gateway. Every profile owns a persistent transcoded
RTMP/RTMPS output and can change intentional viewer delay independently.

## Architecture

The UI only exchanges typed snapshots and requests with `ApplicationController`.
The controller owns an explicit relay state machine and coordinates these
independent workers:

```text
OBS profiles -- RTMP or SRT/MPEG-TS --> shared MediaMTX gateway
                                               |
                                    unique loopback RTMP paths
                                               |
             RelaySession[N] -> IngestReader -> TimestampNormalizer -> PacketBuffer
                                                        |
           held frame / image / text <- DelayController <- source selector
                                                        |
             decoder -> scaler/resampler -> persistent encoders
                                                        |
                                      FLV OutboundPublisher -> platform
```

`PacketBuffer` stores copied compressed packets on a normalized, session-neutral
media timeline. Its keyframe index is authoritative for forward jumps.
`OutboundPublisher` owns a separate monotonic clock. Both source frames and
generated filler pass through the same persistent encoders, so switching sources
does not splice incompatible codec state.

Thread ownership is explicit: Qt owns the process monitor; ingest and publisher
use cooperative `std::jthread` workers; queues and the packet buffer are bounded.
No FFmpeg object crosses into the UI.

## Profiles and concurrent relays

A profile contains its source protocol/path, SRT protection, destination,
intentional delay, buffer limits, encoder/output format, standby visual and
delay-increase text. Destination keys and SRT passphrases use separate
operating-system keychain entries keyed by the profile UUID. Existing
single-stream settings migrate into a `Default` profile.

Profiles share TCP 1935 and UDP 8890 by default. Unique random paths and SRT
passphrases isolate sources, so additional public ports are not needed. Each
active profile has an independent buffer, decoder, encoder, publisher, state
machine and metrics collector. The Streams tab can start or stop profiles
individually or together.

The Capacity tab benchmarks the workload the user specifies. It probes the
selected encoder, measures encoding throughput, tests concurrent encoder
contexts, accounts for current available RAM and applies a configurable safety
margin. Run it separately for NVENC and `libx264` to understand both normal
hardware-encoding capacity and software-fallback capacity.

## Dependency strategy

- Qt 6.5+ (`Core`, `Widgets`, `Network`) from the system or a Windows Qt SDK.
- FFmpeg development libraries discovered with pkg-config. Windows builds may
  provide a `PKG_CONFIG_PATH` for a shared FFmpeg distribution.
- MediaMTX is a bundled, unmodified sidecar. Put `mediamtx.exe` (Windows) or
  `mediamtx` (Linux) in `resources/mediamtx/<platform>/`. It terminates RTMP or
  encrypted SRT and exposes the selected contribution only through loopback RTMP
  to the media engine. Its administration API remains on localhost.
- QtKeychain is used when CMake finds it. Without it, keys remain in memory unless
  the user explicitly accepts insecure persistence (not enabled by default).
- GoogleTest is a build-time dependency only.

## Build

```sh
./build-linux.sh
./build/RTMPTimeShiftProxy
```

See [README-LINUX.md](README-LINUX.md) for LAN ingest, firewall configuration,
optional integration tests, and recommended buffer memory for each delay.

## Milestones

1. Buildable CMake/Qt shell, configuration, state machine, and core tests.
2. Managed MediaMTX lifecycle, publisher discovery, log capture, and diagnostics.
3. H.264/AAC ingest with reconnect-safe timestamp normalization and bounded
   compressed packet buffer.
4. Persistent fixed-profile filler publisher and real-content transcode path.
5. Dynamic increase by cursor hold/filler; decrease by indexed keyframe jump and
   audio alignment.
6. Reconnect/backoff, metrics, rotating redacted logs, integration harness.
7. Windows portable/installer and Linux AppImage assembly.

The repository intentionally does not include third-party MediaMTX, FFmpeg, or Qt
binaries. Packaging scripts validate and copy them from explicitly supplied paths.

## Current implementation status

This checkout includes the UI, controller/state machine, MediaMTX lifecycle,
RTMP and encrypted SRT contribution ingest, reconnect-safe H.264/AAC timestamp
normalization, bounded packet buffer, keyframe-aligned delay selection,
decoded-source/filler switching through persistent H.264/AAC encoders, metrics,
redacted rotating logs, tests, and packaging metadata. The integration suite
tests the complete RTMP relay and encrypted SRT protocol-conversion paths,
including a video-only contribution and a full ingest-reader restart.
