#!/usr/bin/env python3
"""NEXUS-OS / Prism — serveur web de test.

Boote l'ISO Prism dans QEMU (côté serveur) et diffuse la console série vers le
navigateur (Server-Sent Events). Permet aussi d'envoyer des frappes clavier au shell.
Stdlib uniquement — aucune dépendance externe.

Usage:  python3 WEB/server.py [--iso build/nexus-os.iso] [--port 8080] [--smp 4] [--mem 512]
Puis ouvrir http://localhost:8080
"""
import argparse, os, subprocess, threading, time, json, html
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


class QemuSession:
    """Une session QEMU unique, sa sortie série bufferisée, son entrée clavier."""
    def __init__(self, iso, smp, mem):
        self.iso, self.smp, self.mem = iso, smp, mem
        self.proc = None
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.gen = 0            # incrémenté à chaque (re)boot -> invalide les flux SSE

    def running(self):
        return self.proc is not None and self.proc.poll() is None

    def boot(self):
        self.kill()
        cmd = [
            "qemu-system-x86_64",
            "-cdrom", self.iso,
            "-smp", str(self.smp), "-m", str(self.mem),
            "-serial", "stdio", "-display", "none", "-no-reboot",
            # NB: pas de isa-debug-exit -> QEMU reste vivant (démo interactive).
        ]
        with self.lock:
            self.buf = bytearray()
            self.gen += 1
        self.proc = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, bufsize=0, cwd=ROOT)
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        p = self.proc
        while True:
            chunk = p.stdout.read(256)
            if not chunk:
                break
            with self.lock:
                self.buf.extend(chunk)
                if len(self.buf) > 512 * 1024:            # borne mémoire
                    self.buf = self.buf[-256 * 1024:]

    def output(self, offset):
        with self.lock:
            if offset > len(self.buf):
                offset = 0
            return bytes(self.buf[offset:]), len(self.buf), self.gen

    def send(self, text):
        if self.running():
            try:
                self.proc.stdin.write(text.encode())
                self.proc.stdin.flush()
            except (BrokenPipeError, OSError):
                pass

    def kill(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.proc = None


SESSION = None


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass  # silencieux

    def _send(self, code, ctype, body):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/" or self.path.startswith("/index"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                self._send(200, "text/html; charset=utf-8", f.read())
        elif self.path.startswith("/stream"):
            self._stream()
        elif self.path == "/status":
            body = json.dumps({"running": SESSION.running(),
                               "smp": SESSION.smp, "mem": SESSION.mem}).encode()
            self._send(200, "application/json", body)
        else:
            self._send(404, "text/plain", b"not found")

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        data = self.rfile.read(n) if n else b""
        if self.path == "/boot":
            SESSION.boot()
            self._send(200, "application/json", b'{"ok":true}')
        elif self.path == "/reset":
            SESSION.boot()
            self._send(200, "application/json", b'{"ok":true}')
        elif self.path == "/input":
            try:
                txt = json.loads(data).get("data", "")
            except json.JSONDecodeError:
                txt = ""
            SESSION.send(txt)
            self._send(200, "application/json", b'{"ok":true}')
        else:
            self._send(404, "text/plain", b"not found")

    def _stream(self):
        """Server-Sent Events : diffuse la console série au fil de l'eau."""
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.end_headers()
        offset = 0
        my_gen = None
        try:
            while True:
                chunk, total, gen = SESSION.output(offset)
                if my_gen is None:
                    my_gen = gen
                if gen != my_gen:                       # reboot -> on repart de zéro
                    my_gen = gen
                    offset = 0
                    self.wfile.write(b"event: reset\ndata: \n\n")
                    self.wfile.flush()
                    continue
                if chunk:
                    offset = total
                    payload = json.dumps(chunk.decode("utf-8", "replace"))
                    self.wfile.write(f"data: {payload}\n\n".encode())
                    self.wfile.flush()
                else:
                    time.sleep(0.1)
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    global SESSION
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=os.path.join(ROOT, "build/nexus-os.iso"))
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--mem", type=int, default=512)
    ap.add_argument("--host", default="127.0.0.1")
    args = ap.parse_args()

    if not os.path.exists(args.iso):
        raise SystemExit(f"ISO introuvable: {args.iso} (lance 'make iso' d'abord)")

    SESSION = QemuSession(args.iso, args.smp, args.mem)
    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Prism web tester -> http://{args.host}:{args.port}  (ISO={args.iso}, smp={args.smp})")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        SESSION.kill()


if __name__ == "__main__":
    main()
