# RemoteOS-SDL

RemoteOS-SDL is a standalone SDL display, input, audio, image, font, file-transfer,
capture, and instrumentation service for language-oriented operating systems.
It is the shared host companion for PythonOS and RubyOS; neither guest links SDL.

The service speaks RemoteOS protocol v2 over TCP or a Unix-domain socket. The
protocol is intentionally language-neutral: a four-byte big-endian JSON length,
a UTF-8 request or response envelope, and an optional binary trailer. See
[PROTOCOL.md](PROTOCOL.md) for the stable contract.

## Build and test

Dependencies are a C11 compiler, `pkg-config`, SDL2, SDL2_image, SDL2_ttf, and
Python 3 for the protocol smoke test.

```sh
make
make test
```

On macOS:

```sh
brew install pkg-config sdl2 sdl2_image sdl2_ttf
make test
```

## Run

When the guest connects to the service:

```sh
remoteos-sdl --listen-tcp 127.0.0.1:17010
```

When the guest exposes its own display endpoint:

```sh
remoteos-sdl --connect-tcp 192.0.2.10:17010
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
