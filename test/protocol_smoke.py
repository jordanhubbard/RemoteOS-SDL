#!/usr/bin/env python3
"""Exercise the language-neutral protocol and SDL resource lifecycle."""

from __future__ import annotations

import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time


def receive_exact(connection: socket.socket, length: int) -> bytes:
    result = bytearray()
    while len(result) < length:
        chunk = connection.recv(length - len(result))
        if not chunk:
            raise RuntimeError("service closed the protocol stream")
        result.extend(chunk)
    return bytes(result)


def call(connection: socket.socket, request_id: int, operation: str, **params):
    request = json.dumps(
        {"v": 2, "id": request_id, "op": operation, "params": params},
        separators=(",", ":"),
    ).encode()
    connection.sendall(struct.pack(">I", len(request)) + request)
    length = struct.unpack(">I", receive_exact(connection, 4))[0]
    response = json.loads(receive_exact(connection, length))
    assert response["v"] == 2 and response["id"] == request_id, response
    assert response["ok"], response
    return response["result"]


def exchange(connection: socket.socket, envelope: dict) -> dict:
    request = json.dumps(envelope, separators=(",", ":")).encode()
    connection.sendall(struct.pack(">I", len(request)) + request)
    length = struct.unpack(">I", receive_exact(connection, 4))[0]
    return json.loads(receive_exact(connection, length))


def main() -> int:
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="remoteos-sdl-") as temporary:
        endpoint = str(Path(temporary) / "service.sock")
        environment = os.environ.copy()
        environment.setdefault("REMOTEOS_SDL_MODE", "headless")
        environment.setdefault("SDL_VIDEODRIVER", "dummy")
        environment.setdefault("SDL_AUDIODRIVER", "dummy")
        process = subprocess.Popen([executable, "--listen", endpoint], env=environment)
        try:
            for _ in range(200):
                if Path(endpoint).exists():
                    break
                if process.poll() is not None:
                    raise RuntimeError(f"service exited with {process.returncode}")
                time.sleep(0.01)
            else:
                raise RuntimeError("service did not create its socket")

            with socket.socket(socket.AF_UNIX) as connection:
                connection.connect(endpoint)
                rejected = exchange(connection, {
                    "v": 1, "id": 99, "op": "hello",
                    "params": {"protocol": 1, "client": "obsolete"},
                })
                assert not rejected["ok"] and rejected["error"]["code"] == 100
                hello = call(connection, 1, "hello", protocol=2,
                             client="protocol-smoke/1")
                assert hello["service"] == "remoteos-sdl", hello
                assert "audio.pcm" in hello["features"], hello
                assert hello["limits"]["batch_ops"] >= 1, hello
                assert call(connection, 2, "ping", tag="shared")["tag"] == "shared"
                display = call(connection, 3, "display.open", w=96, h=64,
                               title="RemoteOS protocol smoke")
                framebuffer = display["fb_handle"]
                call(connection, 4, "surface.fill_rect", handle=framebuffer,
                     rgb=0x4A154B, rect={"x": 0, "y": 0, "w": 96, "h": 64})
                assert call(connection, 5, "frame.commit")["events"] == []
                metrics = call(connection, 6, "telemetry.snapshot")
                assert metrics["requests"] >= 6 and metrics["presents"] == 1, metrics
                assert metrics["rx_bytes"] > 0 and metrics["tx_bytes"] > 0, metrics
                call(connection, 7, "display.close")
                call(connection, 8, "shutdown")
            assert process.wait(timeout=5) == 0
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
    print("RemoteOS protocol-v2 SDL smoke: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
