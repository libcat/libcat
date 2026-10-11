"""Run job-local HTTP and HTTPS endpoints for the existing network tests."""

import json
import os
import socket
import ssl
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from socketserver import TCPServer
from typing import cast


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self) -> None:
        # The timeout test uses a 1 ms deadline; its peer must respond later.
        time.sleep(0.05)
        body = b"libcat HTTP and HTTPS fixture\n"
        try:
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, format: str, *args: object) -> None:
        pass


class Server(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = socket.SOMAXCONN

    def __init__(self, context: ssl.SSLContext | None = None) -> None:
        self.context = context
        super().__init__(("127.0.0.1", 0), Handler)

    def server_bind(self) -> None:
        # A numeric loopback identity needs no potentially blocking reverse DNS.
        TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address

    def get_request(self) -> tuple[socket.socket, tuple[str, int]]:
        connection, address = super().get_request()
        connection.settimeout(60)
        if self.context is not None:
            # TLS negotiation belongs to the request thread, not the accept loop.
            connection = self.context.wrap_socket(connection, server_side=True, do_handshake_on_connect=False)
        return connection, cast(tuple[str, int], address)


def main(directory: Path) -> None:
    print("Generating the job-local TLS certificate", flush=True)
    directory.mkdir(parents=True, exist_ok=True)
    certificate = directory / "cert.pem"
    key = directory / "key.pem"
    with (directory / "certificate.log").open("w") as output:
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
             "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1",
             "-keyout", str(key), "-out", str(certificate)],
            check=True, stdout=output, stderr=output,
        )
    key.chmod(0o600)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certificate, key)
    print("Starting HTTP and HTTPS listeners", flush=True)
    http = Server()
    https = Server(context)
    for server in (http, https):
        threading.Thread(target=server.serve_forever, daemon=True).start()
    environment = {
        "TEST_REMOTE_HTTP_SERVER_HOST": "localhost",
        "TEST_REMOTE_HTTP_SERVER_PORT": str(http.server_port),
        "TEST_REMOTE_HTTP_SERVER_KEYWORD": "libcat",
        "TEST_REMOTE_HTTPS_SERVER_HOST": "localhost",
        "TEST_REMOTE_HTTPS_SERVER_PORT": str(https.server_port),
        "TEST_REMOTE_HTTPS_SERVER_KEYWORD": "libcat",
        "LIBCAT_TEST_CERT_FILE": str(certificate.resolve()),
    }
    if "GITHUB_ENV" in os.environ:
        with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as output:
            for name, value in environment.items():
                output.write(f"{name}={value}\n")
    (directory / "ready.json").write_text(json.dumps(environment), encoding="utf-8")
    print("HTTP and HTTPS fixtures are ready", flush=True)
    threading.Event().wait()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: ci-fixtures.py <directory>")
    main(Path(sys.argv[1]))
