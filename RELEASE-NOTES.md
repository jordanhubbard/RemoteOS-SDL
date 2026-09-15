# RemoteOS-SDL v0.1.0

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
