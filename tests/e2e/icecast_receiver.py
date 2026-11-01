import base64
import json
import socketserver
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "out" / "icecast"


class Behavior:
    def __init__(self):
        self.lock = threading.Lock()
        self.reject_put = False
        self.drop_after = 0.0
        self.credentials = ("source", "prueba")
        self.connections: list[dict] = []
        self.metadata: list[dict] = []

    def snapshot(self) -> dict:
        with self.lock:
            return {"connections": [dict(c) for c in self.connections], "metadata": list(self.metadata)}


behavior = Behavior()


def read_head(stream) -> tuple[str, dict[str, str]]:
    request_line = stream.readline().decode("utf-8", "replace").strip()
    headers = {}
    while True:
        line = stream.readline().decode("utf-8", "replace")
        if line in ("\r\n", "\n", ""):
            break
        name, _, value = line.partition(":")
        headers[name.strip().lower()] = value.strip()
    return request_line, headers


class SourceHandler(socketserver.StreamRequestHandler):
    def handle(self):
        request_line, headers = read_head(self.rfile)
        method, _, rest = request_line.partition(" ")
        mount = rest.split(" ")[0] if rest else ""
        if method == "CONTROL":
            self.control(mount, headers)
            return
        if method == "GET" and mount.startswith("/admin/metadata"):
            with behavior.lock:
                behavior.metadata.append({"query": mount, "authorization": bool(headers.get("authorization"))})
            self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")
            return
        record = {"method": method, "mount": mount, "content_type": headers.get("content-type", ""),
                  "ice_name": headers.get("ice-name", ""), "ice_description": headers.get("ice-description", ""),
                  "bytes": 0, "status": 0, "file": ""}
        with behavior.lock:
            behavior.connections.append(record)
            index = len(behavior.connections)
            expected = "Basic " + base64.b64encode(":".join(behavior.credentials).encode()).decode()
            reject_put = behavior.reject_put
            drop_after = behavior.drop_after
        if headers.get("authorization") != expected:
            record["status"] = 401
            self.wfile.write(b"HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"icecast\"\r\nContent-Length: 0\r\n\r\n")
            return
        if method == "PUT" and reject_put:
            record["status"] = 405
            self.wfile.write(b"HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n")
            return
        if method not in ("PUT", "SOURCE"):
            record["status"] = 400
            self.wfile.write(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
            return
        record["status"] = 200
        if headers.get("expect", "").lower() == "100-continue":
            self.wfile.write(b"HTTP/1.1 100 Continue\r\n\r\n")
        elif method == "SOURCE":
            self.wfile.write(b"HTTP/1.0 200 OK\r\n\r\n")
        self.wfile.flush()
        OUT.mkdir(parents=True, exist_ok=True)
        target = OUT / f"conexion-{index}.bin"
        record["file"] = str(target)
        started = time.monotonic()
        with target.open("wb") as sink:
            while True:
                if drop_after and time.monotonic() - started > drop_after:
                    break
                chunk = self.request.recv(65536)
                if not chunk:
                    break
                sink.write(chunk)
                record["bytes"] += len(chunk)

    def control(self, path: str, headers: dict):
        body = b""
        if path.startswith("/state"):
            body = json.dumps(behavior.snapshot()).encode()
        elif path.startswith("/set"):
            length = int(headers.get("content-length", "0"))
            update = json.loads(self.rfile.read(length) or b"{}")
            with behavior.lock:
                behavior.reject_put = update.get("reject_put", behavior.reject_put)
                behavior.drop_after = update.get("drop_after", behavior.drop_after)
                if "user" in update:
                    behavior.credentials = (update["user"], update["password"])
                if update.get("reset"):
                    behavior.connections.clear()
                    behavior.metadata.clear()
        self.wfile.write(b"HTTP/1.1 200 OK\r\nContent-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8010
    with Server(("127.0.0.1", port), SourceHandler) as server:
        server.serve_forever()
