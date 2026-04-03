"""
Minimal HTTP/1.1 static file server for integration tests.
Usage: python http_serve.py <root_dir> <port>
"""
import http.server
import os
import socketserver
import sys


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: http_serve.py <root_dir> <port>", file=sys.stderr)
        return 2
    root = os.path.abspath(sys.argv[1])
    port = int(sys.argv[2])
    os.chdir(root)

    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

    with socketserver.TCPServer(("127.0.0.1", port), Handler) as httpd:
        httpd.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
