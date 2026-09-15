# RemoteOS-SDL v0.1.1

## The shared desktop service arrives on Linux ARM64

CI now tests and packages Linux x86_64, Linux ARM64 (`aarch64`) and macOS
ARM64. The DGX Spark gets its own standalone archive instead of borrowing
RubyOS's binary. Checksums name the neighboring archive, not an imaginary
`dist/` directory on the recipient's machine. Source launch instructions and
SDL prerequisites are explicit; binaries still dynamically link host libraries.

Protocol v2 is unchanged. This packaging release introduces no new performance
claims or security guarantees. Native ARM64 build, protocol and archive checks
passed on DGX Spark; publishing requires green hosted platform checks.

## The service boundary

One native companion now owns the deliberately boring host concerns: SDL
windows, input, audio, images, fonts, explicit file transfer, and measurements.
PythonOS and RubyOS remain gloriously opinionated about everything above that
line without maintaining two almost-identical piles of C.

Protocol v2 rejects mismatched clients, bounds render batches, supports binary
payloads and one-way operations, and combines present plus input polling with
`frame.commit`. Service telemetry separates transport volume from per-operation
SDL time, because "the desktop felt slow once" is not a benchmark.

Linux and macOS builds are tested and packaged by CI. Remote links remain a
trusted-network concern; use an authenticated tunnel rather than introducing
your new desktop to the entire internet.

This release is the shared service for PythonOS 0.4.1 and RubyOS 0.2.1. Both
clients negotiate protocol v2 and intentionally reject the former v1 contract.

## Executive summary

More hosts, usable checksums, clearer instructions, the same shared service.
[Release v0.1.1](https://github.com/jordanhubbard/RemoteOS-SDL/releases/tag/v0.1.1).
