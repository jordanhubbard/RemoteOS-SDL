#!/usr/bin/env python3
"""Real depth-buffer, clip-plane, video decode/seek/EOF and lifecycle tests."""
import json
import math
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time
from protocol_smoke import receive_exact


def main():
    with tempfile.TemporaryDirectory(prefix="remoteos-media-") as directory:
        root = Path(directory)
        video = root / "clip.avi"
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "color=red:s=64x48:r=5",
                        "-frames:v", "5", "-c:v", "mpeg4", "-bf", "2", str(video)], check=True)
        aac = root / "aac.mp4"
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "color=blue:s=64x48:r=10",
                        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100", "-t", "0.5",
                        "-c:v", "mpeg4", "-c:a", "aac", str(aac)], check=True)
        endpoint = str(root / "socket")
        env = dict(os.environ)
        env.setdefault("SDL_VIDEODRIVER", "dummy")
        env.setdefault("SDL_AUDIODRIVER", "dummy")
        env.setdefault("REMOTEOS_SDL_MODE", "headless")
        process = subprocess.Popen([sys.argv[1], "--listen", endpoint], env=env)
        try:
            deadline = time.monotonic() + 10
            while not Path(endpoint).exists():
                assert process.poll() is None and time.monotonic() < deadline
                time.sleep(.02)
            with socket.socket(socket.AF_UNIX) as connection:
                connection.settimeout(10)
                connection.connect(endpoint)
                sequence = 0

                def call(op, payload=b"", error=False, **params):
                    nonlocal sequence
                    sequence += 1
                    if payload:
                        params["payload_len"] = len(payload)
                    request = json.dumps(dict(v=2, id=sequence, op=op, params=params)).encode()
                    connection.sendall(struct.pack(">I", len(request)) + request + payload)
                    length, = struct.unpack(">I", receive_exact(connection, 4))
                    response = json.loads(receive_exact(connection, length))
                    assert response["id"] == sequence and response["ok"] != error, response
                    return response.get("result") or {}

                hello = call("hello", protocol=2, client="media-smoke")
                assert {"scene3d.render", "video.decode"} <= set(hello["features"])
                fb = call("display.open", w=96, h=64, title="Media test")["fb_handle"]
                def vertices(z, color):
                    return [[-.8, -.8, z, 1, *color], [.8, -.8, z, 1, *color], [0, .8, z, 1, *color]]
                near = vertices(-.5, [0, 1, 0])
                far = vertices(.5, [1, 0, 0])
                backend = call("scene3d.render", handle=fb, vertices=near+far, clear=0)["backend"]
                if env.get("REMOTEOS_3D_BACKEND") == "opengl":
                    assert backend == "opengl-depth", backend
                else:
                    assert backend == "software-depth", backend
                def pixel(name, x=48, y=32):
                    path = root / name
                    call("debug.capture", path=str(path))
                    data = path.read_bytes()
                    offset, = struct.unpack_from("<I", data, 10)
                    width, height = struct.unpack_from("<ii", data, 18)
                    bits, = struct.unpack_from("<H", data, 28)
                    stride = ((width*bits+31)//32)*4
                    row = height-1-y if height > 0 else y
                    start = offset+row*stride+x*(bits//8)
                    return tuple(data[start:start+3])
                assert pixel("depth.bmp") == (0, 255, 0)
                call("scene3d.render", handle=fb, vertices=far+near, clear=0)
                assert pixel("reverse.bmp") == (0, 255, 0)
                call("scene3d.render", handle=fb, vertices=vertices(2, [1, 0, 0]), clear=0x123456)
                assert pixel("clipped.bmp") == (0x56, 0x34, 0x12)
                crossing = [[-2,-.5,0,1,0,0,1],[2,-.5,0,1,0,0,1],[0,2,0,1,0,0,1]]
                call("scene3d.render", handle=fb, vertices=crossing, clear=0)
                assert pixel("crossing.bmp") == (255, 0, 0)
                call("scene3d.render", handle=fb, vertices=[[1, 2, 3]], error=True)
                call("scene3d.render", handle=fb, vertices=vertices(0,[2,0,0]), error=True)
                call("video.open", payload=b"not a movie", error=True)
                clip = call("video.open", payload=video.read_bytes())
                call("surface.destroy", handle=clip["handle"], error=True)
                assert (clip["width"], clip["height"]) == (64, 48)
                frames = []
                for _ in range(10):
                    result = call("video.frame", handle=clip["handle"], destination=fb)
                    if result["eof"]:
                        break
                    frames.append(result["seconds"])
                assert len(frames) == 5 and frames == sorted(frames), frames
                blue, green, red = pixel("video.bmp")
                assert red > 240 and green < 10 and blue < 10
                call("video.seek", handle=clip["handle"], seconds=0)
                assert not call("video.frame", handle=clip["handle"], destination=fb)["eof"]
                call("video.seek", handle=clip["handle"], seconds=-1, error=True)
                call("video.frame", handle=fb, destination=fb, error=True)
                call("video.close", handle=clip["handle"])
                call("video.frame", handle=clip["handle"], destination=fb, error=True)
                call("encoder.open", width=95, height=64, fps=10, error=True)
                encoder = call("encoder.open", width=96, height=64, fps=10, audio=True)["handle"]
                call("encoder.read", handle=encoder, offset=0, error=True)
                call("encoder.frame", handle=encoder, source=fb, payload=b"bad", error=True)
                pcm = b"".join(struct.pack("<hh", *([int(4000*math.sin(2*math.pi*440*i/48000))]*2)) for i in range(4800))
                for frame in range(10):
                    call("surface.fill_rect", handle=fb, rgb=0xff0000 if frame < 5 else 0x0000ff)
                    call("encoder.frame", handle=encoder, source=fb, payload=pcm)
                size = call("encoder.finish", handle=encoder)["bytes"]
                call("encoder.frame", handle=encoder, source=fb, payload=pcm, error=True)
                encoded = bytearray()
                while True:
                    part = call("encoder.read", handle=encoder, offset=len(encoded))
                    encoded.extend(bytes.fromhex(part["data"]))
                    if part["eof"]:
                        break
                assert len(encoded) == size and size > 1000
                movie = root / "export.mkv"
                movie.write_bytes(encoded)
                info = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-show_streams", "-of", "json", str(movie)]))
                assert {s["codec_name"] for s in info["streams"]} == {"mpeg4", "pcm_s16le"}, info
                decoded_pcm = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(movie), "-map", "0:a:0", "-f", "s16le", "-"])
                assert decoded_pcm == pcm*10, "PCM changed in export"
                call("encoder.close", handle=encoder)
                call("encoder.read", handle=encoder, offset=0, error=True)
                movie_handle = call("video.open", payload=encoded)["handle"]
                call("video.play", handle=movie_handle)
                time.sleep(.15)
                status = call("video.tick", handle=movie_handle, destination=fb)
                assert status["playing"] and 0.05 < status["seconds"] < .5, status
                assert pixel("playing-red.bmp")[2] > 240
                call("video.pause", handle=movie_handle)
                paused = call("video.tick", handle=movie_handle, destination=fb)["seconds"]
                time.sleep(.1)
                assert call("video.tick", handle=movie_handle, destination=fb)["seconds"] == paused
                call("video.seek", handle=movie_handle, seconds=.7)
                status = call("video.tick", handle=movie_handle, destination=fb)
                assert not status["playing"] and status["seconds"] == .7
                assert pixel("seek-blue.bmp")[0] > 240
                call("video.play", handle=movie_handle)
                deadline = time.monotonic()+3
                while not call("video.tick", handle=movie_handle, destination=fb)["eof"]:
                    assert time.monotonic() < deadline
                    time.sleep(.02)
                call("video.close", handle=movie_handle)
                # Exercise compressed mono audio, resampling and codec priming.
                compressed = call("video.open", payload=aac.read_bytes())["handle"]
                call("video.play", handle=compressed)
                assert call("video.tick", handle=compressed, destination=fb)["playing"]
                call("video.pause", handle=compressed)
                call("video.seek", handle=compressed, seconds=.2)
                call("video.play", handle=compressed)
                deadline = time.monotonic()+3
                while not call("video.tick", handle=compressed, destination=fb)["eof"]:
                    assert time.monotonic() < deadline
                    time.sleep(.02)
                call("video.close", handle=compressed)
                assert "scene3d.render" in call("telemetry.snapshot")["ops"]
                call("display.close")
                call("shutdown")
            assert process.wait(timeout=5) == 0
            print(f"RemoteOS 3D ({backend}), FFmpeg decode/encode and synchronized A/V: PASS")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
