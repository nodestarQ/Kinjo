"""Serves the boards' serial ports to the web app over a local WebSocket.

For systems where the browser's Web Serial doesn't work (we hit "device has been lost" with
Chrome on Linux). Set PUBLIC_SERIAL_HELPER=ws://127.0.0.1:8765 in web/app/.env to use it.

  python serial_helper.py            # listens on 127.0.0.1:8765
  python serial_helper.py --allow-origin https://my-kinjo.example   # another site may use it too

  GET  ws://127.0.0.1:8765/ports              one text message: JSON list of ports
  GET  ws://127.0.0.1:8765/open?path=/dev/... raw bytes both ways until either side closes

Only accepts connections from this machine, and only from the Kinjo web app's origins: any other
website open in the browser could otherwise talk to the boards.
"""

import argparse
import asyncio
import json
import sys
import threading
from urllib.parse import parse_qs, urlparse

import serial
from serial.tools import list_ports
from websockets.asyncio.server import ServerConnection, serve

HOST, PORT = "127.0.0.1", 8765
ORIGINS = ["http://127.0.0.1:5173", "http://localhost:5173", "https://kinjo.nodestarq.com"]


def ports() -> list[dict]:
    out = []
    for p in list_ports.comports():
        if p.vid is None:
            continue  # built-in ttyS ports
        out.append({"path": p.device, "description": p.description, "serial": p.serial_number or "",
                    "vid": p.vid, "pid": p.pid})
    return sorted(out, key=lambda p: p["path"])


def open_port(path: str) -> serial.Serial:
    s = serial.Serial()
    s.port = path
    s.baudrate = 115200
    s.timeout = 0.05
    s.dtr = False  # keep the DevKitC out of reset
    s.rts = False
    s.open()
    return s


async def bridge(ws: ServerConnection, path: str) -> None:
    try:
        port = open_port(path)
    except serial.SerialException as e:
        await ws.close(1011, f"cannot open {path}: {e}"[:120])
        return
    loop = asyncio.get_running_loop()
    stop = threading.Event()

    def reader() -> None:
        while not stop.is_set():
            try:
                data = port.read(512)
            except serial.SerialException:
                break
            if data:
                asyncio.run_coroutine_threadsafe(ws.send(data), loop)
        asyncio.run_coroutine_threadsafe(ws.close(1011, "port lost"), loop)

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    print(f"opened {path}", flush=True)
    try:
        async for message in ws:
            port.write(message if isinstance(message, bytes) else message.encode())
    finally:
        stop.set()
        thread.join(timeout=1)
        port.close()
        print(f"closed {path}", flush=True)


async def handler(ws: ServerConnection) -> None:
    url = urlparse(ws.request.path)
    print(f"{ws.request.headers.get('Origin', '?')} {url.path}", flush=True)
    if url.path == "/ports":
        await ws.send(json.dumps(ports()))
        await ws.close()
    elif url.path == "/open":
        path = parse_qs(url.query).get("path", [""])[0]
        if path not in {p["path"] for p in ports()}:
            await ws.close(1008, "unknown port")
            return
        await bridge(ws, path)
    else:
        await ws.close(1008, "unknown path")


async def main(origins: list[str]) -> None:
    async with serve(handler, HOST, PORT, max_size=2**16, origins=origins):
        print(f"serial helper on ws://{HOST}:{PORT}, for {', '.join(origins)}", flush=True)
        for p in ports():
            print(f"  {p['path']:14} {p['description']} {p['serial']}", flush=True)
        await asyncio.get_running_loop().create_future()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--allow-origin", action="append", default=[], help="another web app origin to accept")
    args = ap.parse_args()
    try:
        asyncio.run(main(ORIGINS + args.allow_origin))
    except KeyboardInterrupt:
        sys.exit(0)
