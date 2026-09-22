# RemoteOS-SDL v0.3.0

## The guest stops guessing how big the screen is

A bare-metal guest has no environment, no display enumeration, and no way to
ask what the host is plugged into. So it asks for a number it picked at compile
time — RubyOS asks for 1024x768 — and lives with it however large the monitor
actually is. That was always the wrong side of the wire to decide on.

`REMOTEOS_SDL_SIZE=WxH` now lets whoever launches the service pick the size,
and `display.open` reports back the framebuffer it actually created. The guest
is expected to adopt that answer rather than assume it got what it asked for;
PythonOS already did, and RubyOS now does too. Malformed or absurd values are
logged and ignored instead of failing the open, and the override is skipped in
headless mode so captures and visual goldens keep the dimensions their callers
chose.

## A latent bug on the fallback path

`display.open` previously reported the size it was *asked* for. On the renderer
path that was accidentally correct, because the framebuffer is created at
exactly those dimensions. On the window-surface fallback it was not:
`SDL_GetWindowSurface` returns a surface at the drawable size, so on a
high-density display a guest was told a size smaller than the buffer it had
been handed, and would have painted into one corner of it. The reply now always
describes the framebuffer, whichever path produced it.

## Pixel density

Windows are created with `SDL_WINDOW_ALLOW_HIGHDPI`, and the renderer is pinned
to the guest's logical size. The guest keeps drawing in the coordinates it asked
for while the scale-up happens once, in the renderer, with the nearest-neighbour
hint the service already sets — instead of the guest receiving a low-resolution
backing store that the OS then smooths up to fill the window.

On a 1:1 display this is a measured no-op, which is all that could be verified
during development:

```
ALLOW_HIGHDPI=0  window=1024x768  renderer_output=1024x768
ALLOW_HIGHDPI=1  window=1024x768  renderer_output=1024x768
```

The high-density path is therefore **untested**. Someone on a Retina or scaled
display should confirm that mouse coordinates and framebuffer dimensions stay
correct before relying on it.

## Compatibility

No protocol change: this is still protocol v2, and `display.open` takes the
same parameters. A guest that ignores the reported size keeps working exactly
as before, at the size it requested, unless an operator sets
`REMOTEOS_SDL_SIZE`.
