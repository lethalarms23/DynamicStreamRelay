# Linux build, LAN ingest, and buffer sizing

## Build

The build script creates a directly runnable executable and does not create a ZIP
or other archive:

```sh
./build-linux.sh
./build/RTMPTimeShiftProxy
```

It expects CMake 3.24+, a C++20 compiler, Qt 6.5+ development files, FFmpeg
development libraries, pkg-config, and GoogleTest. The required FFmpeg libraries
are `libavformat`, `libavcodec`, `libavutil`, `libswscale`, and `libswresample`.

Unit tests run by default. To include the local RTMP integration test:

```sh
RTSP_BUILD_INTEGRATION_TESTS=ON ./build-linux.sh
```

To build without tests or to choose another build directory:

```sh
RTSP_BUILD_TESTS=OFF RTSP_LINUX_BUILD_DIR="$PWD/build-release" ./build-linux.sh
```

## Choosing RTMP or SRT ingest

Use **RTMP** when OBS and the relay are on the same reliable LAN. Use **SRT**
for Wi-Fi, mobile connections, the public internet, or a remote contribution
encoder. SRT recovers lost packets within its configured latency window and can
encrypt the contribution link. It does not change the app's destination:
Twitch, YouTube, and other platforms still receive the persistent RTMP/RTMPS
output.

SRT recovery latency and intentional stream delay are separate:

- **SRT recovery latency** is normally 1,500–3,000 ms and protects the network
  contribution from packet loss.
- **Requested delay** is the viewer-facing timeshift controlled from the Delay
  tab and can be changed while live.

For SRT in OBS:

1. Select **SRT contribution** in the app and enable LAN connections.
2. Enter the relay machine's LAN address or public DNS name in **Advertised
   host**. Do not use `0.0.0.0`.
3. Keep encryption enabled and copy the displayed **Full ingest URL**.
4. In OBS, choose **Custom** service, paste the full SRT URL into **Server**, and
   leave **Stream Key** empty.
5. Use H.264 video. AAC audio is recommended; when the contribution has no audio,
   the relay generates silence without rejecting the video.

The generated URL contains the private path and SRT encryption passphrase. Treat
it like a stream key. The app hides it by default, redacts it from logs, and
stores the passphrase in the system keychain when available.

## Connecting OBS from a Windows PC over RTMP

`127.0.0.1` accepts connections only from the Linux machine itself. In the
application, enable **Allow OBS connections from the local network**. MediaMTX
then listens on `0.0.0.0`, which means all Linux network interfaces.

Do not enter `0.0.0.0` in OBS. OBS must use the Linux machine's LAN address:

```text
rtmp://192.168.1.50:1935/live
```

Use the separately displayed local stream key in OBS. You can find the Linux LAN
addresses with:

```sh
hostname -I
```

If several addresses are shown, normally choose the address in the same subnet
as the Windows PC. Avoid Docker, VPN, virtual-machine, and loopback addresses.
Giving the Linux machine a DHCP reservation prevents this address from changing.

No router port forwarding is required when both computers are on the same LAN.
Do not expose port 1935 to the internet. Only permit TCP port 1935 through the
Linux firewall, preferably restricted to the Windows PC:

```sh
sudo ufw allow from WINDOWS_PC_IP to any port 1935 proto tcp
```

MediaMTX's administration API remains bound to `127.0.0.1` and must not be
opened in the firewall.

## SRT firewall and internet routing

SRT uses UDP. On a LAN, allow only the Windows OBS machine to reach the configured
SRT port (8890 by default):

```sh
sudo ufw allow from WINDOWS_PC_IP to any port 8890 proto udp
```

No router port forwarding is needed when both machines are on the same LAN.

For an internet contribution, the relay must be reachable at its advertised
public hostname/IP and UDP port:

- On a VPS or cloud server, allow the SRT UDP port in both the host firewall and
  the provider's network/security-group firewall.
- If the relay is behind a home router, forward only the configured SRT UDP port
  to the Linux relay machine and give that machine a stable LAN address.
- Do not expose the internal RTMP port or MediaMTX administration API merely to
  use SRT. Those remain local to the relay architecture.

OBS acts as the SRT caller, so the OBS/truck side normally needs only outbound
UDP access. Increase SRT recovery latency for unstable links; this adds network
latency but does not consume the intentional timeshift buffer.

## Multiple source profiles

All profiles normally share the same forwarded SRT UDP port. MediaMTX routes
each caller by its unique `streamid` path:

```text
srt://stream.example.com:8890?streamid=publish:live/girlfriend-key...
srt://stream.example.com:8890?streamid=publish:live/truck-key...
```

Do not create a router forwarding rule for every profile. Forward UDP 8890 once
to the relay PC, then use the full URL shown for each selected profile. A LAN
profile can advertise the relay's `192.168.x.x` address while a truck profile
advertises public DNS; the listener and port remain shared.

For the truck profile, use encrypted SRT and begin with 2000–4000 ms recovery
latency. Packet loss recoverable inside that window does not affect the
intentional Delay setting. A longer source interruption switches only that
profile to its configured held frame or standby image; other profiles remain
unaffected.

Use the Capacity tab before enabling several automatic-start profiles. Benchmark
the expected 1080p resolution, FPS and bitrate with the exact encoder selected
in those profiles. Benchmark NVENC and Software H.264 separately. Run the test
while relays are stopped unless intentionally measuring remaining headroom.

## Maximum buffer memory

The ring buffer stores the compressed stream received from OBS. Its memory
requirement is based on the **OBS video and audio bitrate**, not the bitrate
selected for the outbound encoder.

Approximate compressed data:

```text
MiB = (video Kbit/s + audio Kbit/s) × delay seconds / 8388.608
```

Allow additional space for packet objects, codec metadata, bitrate variation,
and keyframe alignment. The table below includes practical safety margin.

| Maximum delay | 6,000 + 160 Kbit/s | 8,000 + 160 Kbit/s | 12,000 + 160 Kbit/s |
|---:|---:|---:|---:|
| 30 seconds | 64 MiB | 64 MiB | 96 MiB |
| 60 seconds | 96 MiB | 96 MiB | 128 MiB |
| 120 seconds | 160 MiB | 192 MiB | 256 MiB |
| 180 seconds | 256 MiB | 256 MiB | 384 MiB |
| 300 seconds | 384 MiB | 384 MiB | 640 MiB |

For the default OBS rate of 6,000 Kbit/s video and 160 Kbit/s audio, use:

- 96 MiB for up to 60 seconds;
- 160 MiB for up to 120 seconds;
- 256 MiB for up to 180 seconds;
- 384 MiB for up to 300 seconds.

Set **Maximum buffer duration** to at least the largest delay you intend to use.
Reducing buffer memory does not reduce resolution, frame rate, bitrate, or image
quality. If the configured limit is too small, the oldest buffered packets are
discarded and the requested delay may no longer be available.

The Delay tab also provides presets for 0, 30, 60, 120, 180, and 300 seconds.
Each preset applies the recommended buffer duration and memory automatically.
The presets are locked while the relay is live; dynamic live changes continue to
use the delay slider and Apply button.

While the outbound relay is stopped, a 0-second requested delay retains no OBS
packets. A nonzero requested delay retains only the requested window plus a small
keyframe-alignment margin. While active, even the zero-delay preset retains a
six-second recovery window so LAN jitter or a decoder stall cannot immediately
evict the keyframe needed to return to live content. This recovery history is not
intentional viewer delay.

The buffer limit is not a limit for the entire process. FFmpeg decoders,
encoders, video frames, Qt, graphics drivers, and the system allocator require
additional memory. The Metrics tab shows the compressed buffer usage separately.
