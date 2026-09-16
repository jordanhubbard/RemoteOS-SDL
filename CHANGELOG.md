# Changelog

## [Unreleased]

## [0.2.1] - 2026-09-15

- Render OpenGL scenes into explicit offscreen framebuffer attachments rather
  than a hidden window's undefined default framebuffer. Reproduced and verified
  on the DGX Spark NVIDIA GB10, alongside Mesa/Xvfb and software tests.
- Load core/EXT framebuffer entry points for the OpenGL 2.1 host boundary.

## [0.2.0] - 2026-09-15

- Add language-neutral depth-buffered triangle rendering with OpenGL and
  software backends, homogeneous clipping and bounded geometry.
- Add FFmpeg clip decoding, keyframe seeking, audio-clocked playback with
  pause/resume, and bounded Matroska export with MPEG-4 video and stereo PCM.
- Keep protocol v2; advertise additive capabilities and retain per-operation
  telemetry. Close media resources on disconnect and validate typed handles.
- Add Linux/macOS FFmpeg dependencies and real codec, A/V clock, export round-trip,
  depth/clipping and Linux OpenGL tests. Keep three-platform release artifacts.

## [0.1.1] - 2026-09-15

- Add Linux ARM64 to CI and release packages alongside Linux x86_64 and macOS.
- Make archive checksums verifiable beside the downloaded archive.
- Clarify source-tree launch paths, SDL dependencies, and architecture choices.

## [0.1.0] - 2026-09-14

- Establish the RemoteOS protocol v2 service shared by PythonOS and RubyOS.
