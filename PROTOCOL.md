# RemoteOS protocol v2

## Framing

Every JSON envelope is preceded by a four-byte unsigned big-endian length. The
maximum envelope is 16 MiB. If `params.payload_len` is present, exactly that many
binary bytes immediately follow the JSON envelope.

```json
{"v":2,"id":17,"op":"display.open","params":{"w":640,"h":480}}
{"v":2,"id":17,"ok":true,"result":{"handle":1}}
{"v":2,"id":17,"ok":false,"error":{"code":4,"msg":"w/h required"}}
```

IDs greater than zero are request/response correlations. ID zero is an ordered
one-way notification and has no response. A later round trip is a transport-order
barrier. Integer handles belong to one connection and expire with the session.

## Negotiation

The first request must have a positive ID and be `hello` with
`{ "protocol": 2, "client": "implementation-name/version" }`. A mismatched
envelope or negotiation version is rejected. Its result names the service,
publishes resource limits, and advertises features. Clients use advertised
features instead of inferring them from the service version.

The baseline operation families are:

- lifecycle: `hello`, `ping`, `shutdown`;
- display: `display.open`, `display.present`, `display.close`, `frame.commit`;
- surfaces: create, destroy, fill, line, scroll, blit, upload, scaled upload,
  and PNG/JPEG loading;
- text and registered SDL calls: `text.draw`, `sdl.call`;
- input: `event.poll` and automation-only `debug.event.inject`;
- audio: `audio.open`, `audio.queue`, `audio.status`, `audio.close`;
- bounded host file import/export using opaque tokens and 32 KiB chunks;
- diagnostics: `telemetry.snapshot` and `debug.capture`.

`render.batch` accepts only response-free drawing operations, including the
registered fill/blit subset of `sdl.call`. Binary trailers, return-bearing SDL
calls, handle-producing calls, presentation, and lifecycle calls
cannot be batched. The service rejects batches above its negotiated limit.
`frame.commit` combines presentation and event polling. An ordered ID-zero
`display.present` is preferable when no input or acknowledgement is needed.

## Evolution

### Multimedia extensions (service 0.2.0)

The additive v2 features `scene3d.render`, `video.decode`, `video.playback`
and `video.encode` advertise:

| Operation | Parameters / payload | Result |
| --- | --- | --- |
| `scene3d.render` | `handle`, `vertices`, optional RGB `clear` | `backend`, `triangles` |
| `video.open` | `payload_len` and encoded bytes in binary trailer | `handle`, `width`, `height` |
| `video.frame` | video `handle`, surface `destination` | `eof`, `seconds` |
| `video.seek` | video `handle`, `seconds` | empty success |
| `video.close` | video `handle` | empty success |
| `video.play` / `video.pause` | video `handle` | empty success |
| `video.tick` | video `handle`, surface `destination` | `eof`, `seconds`, `playing` |
| `encoder.open` | even `width`, `height`, integer `fps`, boolean `audio` | `handle` |
| `encoder.frame` | encoder `handle`, surface `source`, optional PCM trailer | empty success |
| `encoder.finish` | encoder `handle` | encoded `bytes` count |
| `encoder.read` | finished encoder `handle`, byte `offset` | hex `data`, `eof` |
| `encoder.close` | encoder `handle` | empty success |

Each triangle comprises three `[x,y,z,w,r,g,b]` vertices. Coordinates are
homogeneous clip space (six planes `-w <= x,y,z <= w`), colors are 0..1.
Components must be finite, with coordinate magnitude at most 1e6. Rendering
clears the destination and depth buffer, clips, performs perspective division
and depth testing, and interpolates colors. Maximum 16384 triangles per call,
2048x2048 32-bit target. Empty geometry clears the target. The normal frame-byte
limit also applies. Backend is `opengl-depth` or `software-depth`.

Video accepts at most 16 MiB encoded bytes and four simultaneous handles.
FFmpeg decodes MOV/MP4, Matroska/WebM, AVI, Ogg or GIF containers, subject to
the installed decoder build. No guest-supplied host paths or URLs are accepted.
Source and destination dimensions are bounded at 4096; decoded frames scale
into the destination surface's pixel format. `video.frame` returns one frame
and its presentation time, or `eof: true`; it does not present the display.
Missing timestamps advance using the guessed frame rate (30 fps fallback).
Seek accepts 0..86400 seconds and goes to a preceding keyframe; callers decode
forward to the desired time. Malformed trailer lengths terminate the connection
to avoid framing desynchronization. All video handles close on disconnect.

Timed playback predecodes up to 60 seconds of audio (at most eight input channels)
into 48 kHz stereo S16. Timestamp gaps become silence. Video follows SDL queue
consumption; after audio ends, or for silent clips, it follows monotonic time.
Call `video.tick` regularly to copy due frames, then present the destination.
Pause freezes the clock; seek clears pending frames and replaces queued audio.
Tick decodes at most 120 frames per call to bound catch-up work. Keep a stable
destination size during playback. Seek before switching between timed playback
and manual frame decoding. SDL device latency and tick cadence bound actual sync
precision; this is not physical speaker-clock measurement. A backend may allow
only one audio device: close independent PCM output before starting a movie.

Export produces Matroska with MPEG-4 video and optional PCM S16LE stereo at
48000 Hz. Even dimensions must be 2..2048, fps 1..60 and divide 48000. Each
`encoder.frame` advances one frame and, when audio is enabled, requires exactly
`48000/fps * 4` PCM bytes. Frame calls are ordered; mismatched input is rejected.
Maximum two encoder handles, 60 seconds and 16 MiB encoded output. Finish flushes
the codec and muxer; read returns up to 32768 bytes per chunk as hex. Closing
before finish discards the export. Codec/output failures poison the encoder;
close it and start a new one. Handles also close on disconnect. Use the existing
host file-export API to save bytes under host policy. Streaming is not included.

New operations participate in ordinary per-operation service telemetry.
`REMOTEOS_3D_BACKEND=opengl` requires OpenGL; `software` forces the deterministic
fallback. Unset chooses OpenGL when available, then software. The GL path uses
an explicit offscreen framebuffer in a hidden context with readback, not a
retained GPU scene. Service 0.2.1 fixes the undefined hidden-window framebuffer
behavior observed on NVIDIA GB10 in 0.2.0.

PythonOS and RubyOS advance this protocol and service together. There is no
compatibility facade: breaking semantics increments the version and all clients
move in the same release train. OS names, language types, guest transport choices,
and application policy never belong in the service contract.

## Security boundary

The service can create windows, emit audio, and import or export explicitly
selected files. Protocol v2 has no authentication, confidentiality, or process
sandbox. Bind to loopback by default. Remote deployments must use a private
network or authenticated tunnel until a separately versioned secure session
layer exists.
