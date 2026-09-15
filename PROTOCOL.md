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
