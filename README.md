# RemoteOS-SDL

RemoteOS-SDL is a standalone SDL display, input, audio, image, font, file-transfer,
capture, and instrumentation service for language-oriented operating systems.
It is the shared host companion for PythonOS and RubyOS; neither guest links SDL.

The service speaks RemoteOS protocol v2 over TCP or a Unix-domain socket. The
protocol is intentionally language-neutral: a four-byte big-endian JSON length,
a UTF-8 request or response envelope, and an optional binary trailer. See
[PROTOCOL.md](PROTOCOL.md) for the stable contract.

The current coordinated release train is RemoteOS-SDL 0.2.x, PythonOS 0.4.x,
and RubyOS 0.3.x. Both OS repositories pin this repository as
`services/remoteos-sdl`; copied or language-branded companion binaries are not
part of the architecture.

## Build and test

Dependencies are a C11 compiler, `pkg-config`, SDL2, SDL2_image, SDL2_ttf, OpenGL, FFmpeg development libraries, and
Python 3 plus the ffmpeg CLI for the protocol smoke test.

Clone this repository and enter it before running the commands below.
On Debian/Ubuntu (including WSL2), install `build-essential pkg-config python3
libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libavformat-dev libavcodec-dev
libavutil-dev libswscale-dev libswresample-dev libgl-dev ffmpeg`.

```sh
make
make test
```

On macOS:

```sh
brew install pkg-config sdl2 sdl2_image sdl2_ttf ffmpeg
make test
```

## Run

Release archives contain `bin/remoteos-sdl`; source builds produce
`./remoteos-sdl`. The binaries require the host SDL2, SDL2_image, SDL2_ttf, FFmpeg and OpenGL
libraries. Install the dependencies above before launching either form.
Choose the archive matching your host architecture. Version 0.1.1 publishes
Linux x86_64, Linux ARM64 (`aarch64`), and macOS ARM64 archives. Build from source
or use the canonical service included in the RubyOS Linux ARM64 bundle.

Keep an archive and its `.sha256` file together and run
`sha256sum -c <archive>.sha256` (macOS: `shasum -a 256 -c <archive>.sha256`).
The original 0.1.0 checksum files include a `dist/` prefix; for those files,
place the archive in a `dist` directory and check from its parent directory.

When the guest connects to the service:

```sh
./remoteos-sdl --listen-tcp 127.0.0.1:17010
```

When the guest exposes its own display endpoint:

```sh
./remoteos-sdl --connect-tcp 192.0.2.10:17010
```

Set `REMOTEOS_SDL_MODE=headless` for automation. `REMOTEOS_SDL_SLOW_US`,
`REMOTEOS_SDL_SOCKET_BUFFER`, and `REMOTEOS_SDL_EXPORT_DIR` tune diagnostics,
TCP buffering, and the explicit export destination.

## Design boundary

RemoteOS-SDL owns host policy and SDL resources. Guests own their scene graph,
widgets, applications, filesystem, and scheduling. Adding another guest should
require a protocol client, not another fork of this service.

The hot path uses ordered notifications, bounded render batches, binary payload
trailers, and `frame.commit`, which presents and returns input in one round trip.
`telemetry.snapshot` reports wire volume, request/frame counts, dropped events,
audio queue depth, and per-operation service time.

The protocol is currently intended for loopback, a trusted network, or an SSH
tunnel. It does not provide authentication, encryption, or tenant isolation.
