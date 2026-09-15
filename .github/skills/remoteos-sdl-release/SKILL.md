---
name: remoteos-sdl-release
description: Validate and publish a coordinated RemoteOS-SDL release on Linux and macOS.
---

# RemoteOS-SDL release

Inspect protocol, tests, commits, and user-visible behavior before rewriting
`RELEASE-NOTES.md`. Keep claims factual and explain latency, compatibility,
security, and packaging consequences.

Publishing requires explicit user authorization. Run `scripts/release.sh X.Y.Z`;
do not manually bypass it. The script requires clean `main`, current versioned
notes, local validation, and green Linux/macOS CI before it creates the tag.
The tag workflow builds fresh packages on both platforms and publishes their
archives and checksums. Never publish a one-platform release as complete.
