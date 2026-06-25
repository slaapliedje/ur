#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tiny TCP echo server for testing the FujiNet N:TCP path.

Listens on 0.0.0.0:<port> (default 1234) and echoes back whatever a client
sends. Run it on the SAME machine as FujiNet-PC, then point the game at
N:TCP://localhost:<port>/ (see src/atari/main.c, UR_NET_URL).

Usage:
    python3 tools/echo-server.py [port]
"""
import socketserver
import sys


class EchoHandler(socketserver.BaseRequestHandler):
    def handle(self):
        peer = self.client_address
        data = self.request.recv(4096)
        if data:
            print(f"{peer}: {data!r}")
            self.request.sendall(data)


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 1234
    with Server(("0.0.0.0", port), EchoHandler) as srv:
        print(f"echo server on 0.0.0.0:{port} (Ctrl-C to stop)")
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            print("\nbye")


if __name__ == "__main__":
    main()
