# RemoteOS-SDL v0.2.1

## The actual GPU would like a word

0.2.1 fixes a driver-specific defect found by running the full media test on
the DGX Spark NVIDIA GB10 after Mesa/Xvfb had passed. The hidden window's
default framebuffer was not a valid offscreen rendering target. Explicit color
and depth attachments now make that target independent of window visibility.
The corrected path passes on real NVIDIA hardware and Mesa/Xvfb. This patch
supersedes 0.2.0; its tag and artifacts remain intact for reproducibility.

## The display server has acquired a film department

One OS-neutral service now renders depth-buffered 3D, decodes video, plays
short audiovisual clips against an audio-derived clock, and exports Matroska
movies. PythonOS and RubyOS share the same implementation. Apparently two
language kernels do not require two copies of every codec bug.

Triangle scenes use homogeneous clipping and depth testing through OpenGL
or the software fallback. Video uses FFmpeg; playback follows SDL's consumed
audio samples, with pause/resume and seek. The host does the expensive work;
the guest supplies scenes, timing, surfaces and optional PCM.

Export accepts surfaces at fixed frame times, producing MPEG-4 video and
optional 48 kHz stereo PCM in Matroska. No shell commands, guest host-paths,
or network media URLs are part of that API.

## Actual boundaries, because physics has declined our roadmap

- Protocol v2 gains advertised capabilities; existing desktop clients still work.
- Encoded inputs/outputs: 16 MiB. Four decoder handles, two encoder handles.
- A/V playback predecodes at most 60 seconds of audio. Export caps at 60 seconds.
- OpenGL uses a hidden context and readback; this is not a retained GPU scene.
- Playback requires regular ticks from the client. SDL queue consumption is
  the audio clock, not a promise of sample-exact physical speaker latency.
- No streaming codec pipeline, texture/lighting system, or nonlinear editor.
- The endpoint is still unauthenticated: use loopback or an authenticated tunnel.

## Build, tests and packages

FFmpeg development libraries (including swresample) and OpenGL join the SDL
dependencies. Linux ARM64, Linux x86_64 and macOS ARM64 packages link host
libraries; see README for installation. Release gates test real decode/encode,
PCM round trips, playback clock behavior and software depth/clipping. Linux
also exercises OpenGL under Xvfb. New operations have ordinary service telemetry.

The executive summary: shared devices, real media, explicit limits, and no
language-specific facade. [Release v0.2.1](https://github.com/jordanhubbard/RemoteOS-SDL/releases/tag/v0.2.1).
