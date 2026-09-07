#!/usr/bin/env python3
"""Local portrait preview of the exact RGB565 device framebuffer.

This server has no web simulation: the C++ process owns time, ecology, and pixels.
Requires Pillow only to encode its PPM output for browser display.
"""
from __future__ import annotations

import argparse
import io
import json
import pathlib
import signal
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

ROOT = pathlib.Path(__file__).resolve().parents[1]
HTML = r"""<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Ant farm — quiet observation</title>
<style>
:root{color-scheme:light;font-family:Georgia,'Times New Roman',serif;background:#e8e6de;color:#686a59}
*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;padding:32px}
main{display:flex;align-items:center;gap:56px}.farm{width:min(49.5vh,65vw);aspect-ratio:9/16;background:#d1bd95;box-shadow:0 12px 50px #32382816;outline:9px solid #414d3e;border-radius:2px;overflow:hidden}
img{width:100%;height:100%;display:block;object-fit:contain;image-rendering:auto}
aside{max-width:235px}h1{font-size:30px;font-weight:normal;letter-spacing:-1px;color:#414d3e;margin:0 0 20px}p{font-size:14px;line-height:1.7;margin:12px 0}.label{font:10px/1.5 system-ui,sans-serif;letter-spacing:2px;text-transform:uppercase;color:#7b7b68}
.rule{width:32px;height:1px;background:#b4b6a6;margin:27px 0}details{margin-top:28px;font:11px/1.6 system-ui,sans-serif}summary{cursor:pointer;color:#777b68}pre{white-space:pre-wrap;color:#777b68;font-size:11px}
.status{font:11px/1.6 system-ui,sans-serif;color:#898976;margin-top:24px}.error{color:#8c5f45}
@media(max-width:690px){body{padding:20px}.farm{width:min(51vh,85vw)}main{flex-direction:column;gap:30px}aside{max-width:340px;width:100%;padding:0 5px}h1{font-size:25px;margin-bottom:12px}.rule,aside>p{display:none}details{margin-top:14px}}
@media(prefers-reduced-motion:reduce){*{scroll-behavior:auto}}
</style>
<main><div class="farm"><img id="farm" src="/frame.png" alt="A living ant colony in a portrait cross-section of soil"></div>
<aside><div class="label">An artificial formicarium</div><h1>A little world.</h1>
<p>Watch for a while. The ants choose where to go, gather food, carry soil, and tend their young.</p>
<div class="rule"></div><p>Nothing here needs your attention.<br>The colony keeps its own time.</p>
<div class="status" id="status">Connecting to the colony…</div>
<details><summary>About this preview</summary><p>The image is the C++ renderer’s actual 1080 × 1920 RGB565 output. This surrounding page is a desktop preview and does not appear on the final display.</p><pre id="diagnostics"></pre></details>
</aside></main>
<script>
let stopped=false;const farm=document.querySelector('#farm'),status=document.querySelector('#status');
async function frame(){if(document.hidden){setTimeout(frame,700);return}try{const response=await fetch('/frame.png?t='+Date.now(),{cache:'no-store'});if(response.ok){const url=URL.createObjectURL(await response.blob());const old=farm.dataset.url;farm.src=url;farm.dataset.url=url;if(old)URL.revokeObjectURL(old)}}catch(e){}setTimeout(frame,200)}
async function health(){try{const s=await(await fetch('/status')).json();status.textContent=s.running?'Observing · real time':s.message;status.className=s.running?'status':'status error';document.querySelector('#diagnostics').textContent='Framebuffer: '+s.width+' × '+s.height+'\nProcess: '+(s.running?'running':'stopped')+'\nFrame age: '+s.frame_age_seconds+' s'}catch(e){status.textContent='Preview disconnected'}setTimeout(health,3000)}frame();health();
</script></html>"""


class Frames:
    def __init__(self, path: pathlib.Path):
        from PIL import Image
        self.Image = Image
        self.path = path
        self.lock = threading.Lock()
        self.stamp = None
        self.data = b""
        self.size = (1080, 1920)

    def png(self) -> bytes:
        with self.lock:
            stamp = self.path.stat().st_mtime_ns
            if stamp != self.stamp:
                # The producer atomically renames completed frames into place.
                with self.Image.open(self.path) as im:
                    self.size = im.size
                    stream = io.BytesIO()
                    im.save(stream, format="PNG", compress_level=1)
                    self.data = stream.getvalue()
                self.stamp = stamp
            return self.data


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--bind", default="127.0.0.1", help="Default is local access only")
    parser.add_argument("--binary", type=pathlib.Path, default=ROOT / "build/antfarm")
    parser.add_argument("--weather", type=pathlib.Path, default=ROOT / "data/weather/newcastle_2023_may_aug.csv")
    parser.add_argument("--state-dir", type=pathlib.Path, default=ROOT / "out/preview")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--days", type=float, default=0, help="Initial age when creating a new save")
    parser.add_argument("--no-launch", action="store_true", help="Serve an independently produced frame.ppm")
    args = parser.parse_args()
    args.state_dir = args.state_dir.resolve()
    args.state_dir.mkdir(parents=True, exist_ok=True)
    frame = args.state_dir / "frame.ppm"
    save = args.state_dir / "world.save"
    process = None
    log = None
    if not args.no_launch:
        if not args.binary.is_file():
            parser.error(f"Build the simulation first; executable is missing: {args.binary}")
        if not args.weather.is_file():
            parser.error(f"Weather file is missing: {args.weather}")
        weather = ["--weather", str(args.weather.resolve())]
        if not save.exists():
            subprocess.run([str(args.binary.resolve()), "render", "--days", str(args.days),
                            "--seed", str(args.seed), *weather, "--output", str(frame),
                            "--save", str(save)], check=True, cwd=ROOT)
    frames = Frames(frame)

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            path = urlsplit(self.path).path
            if path == "/":
                self.reply(200, "text/html; charset=utf-8", HTML.encode())
            elif path == "/frame.png":
                try:
                    self.reply(200, "image/png", frames.png())
                except (FileNotFoundError, OSError, ValueError):
                    self.reply(503, "text/plain", b"The first complete frame is being prepared.")
            elif path == "/status":
                running = process is None or process.poll() is None
                data = {"running": running, "width": frames.size[0], "height": frames.size[1],
                        "frame_age_seconds": round(time.time() - frame.stat().st_mtime, 1) if frame.exists() else None,
                        "message": "Simulation stopped; see simulation.log in the state directory."}
                self.reply(200, "application/json", json.dumps(data).encode())
            else:
                self.reply(404, "text/plain", b"Not found")

        def reply(self, code, content_type, body):
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, fmt, *args):
            if args and str(args[1]) not in {"200", "304"}:
                super().log_message(fmt, *args)

    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    try:
        # Bind successfully before launching a producer. A port/permission failure
        # must not leave an orphan process continuously writing framebuffers.
        if not args.no_launch:
            log = (args.state_dir / "simulation.log").open("a")
            process = subprocess.Popen([str(args.binary.resolve()), "live", *weather, "--load", str(save),
                                        "--save", str(save), "--output", str(frame)],
                                       cwd=ROOT, stdout=log, stderr=log)
        print(f"Ant farm preview: http://{args.bind}:{args.port}", flush=True)
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if process is not None and process.poll() is None:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=5)
        if log:
            log.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
