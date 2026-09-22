# Changelog

## [Unreleased]

## [0.3.0] - 2026-09-21

- Let the host pick the desktop size with `REMOTEOS_SDL_SIZE=WxH`, so a
  bare-metal guest is no longer stuck with the resolution it guessed at
  compile time. Ignored in headless mode; malformed values are logged rather
  than fatal.
- Report the framebuffer that `display.open` actually created. This was already
  true on the renderer path and wrong on the window-surface fallback, where
  `SDL_GetWindowSurface` returns a drawable-sized surface and the guest was
  told otherwise.
- Create windows with `SDL_WINDOW_ALLOW_HIGHDPI` and pin the renderer to the
  guest's logical size, so scale-up happens once in the renderer instead of the
  OS smoothing a low-resolution backing store. A no-op on 1:1 displays; the
  high-density path is untested.

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
