#!/usr/bin/env python3
"""Controlled TLS/HTTP peer for the native M10 QEMU integration scenario.

The peer creates an ephemeral private CA and a server certificate for the
QEMU user-network host alias ``host``. Port 18444 presents a certificate
issued by an untrusted CA; port 18445 presents an expired certificate issued
by the trusted CA. Only the normal CA is provisioned as ROOT.DER in the root
of the supplied ESP/system volume using MicaNT crypt32's bundle format.
No certificate or network dependency is retained between runs.
"""

from __future__ import annotations

import shutil
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
from pathlib import Path


class M10TlsPeer:
    """One deterministic HTTPS endpoint, suitable for a context manager."""

    PORT = 18443
    ROGUE_ROOT_PORT = 18444
    EXPIRED_CERTIFICATE_PORT = 18445
    PAYLOAD = b"MICA-M10-HTTPS-OK\n"
    ROOT_BUNDLE_MAGIC = b"MCROOT1\0"
    MAX_REQUEST_BYTES = 8192

    def __init__(self, esp_root: Path, openssl: str | None = None) -> None:
        self.esp_root = Path(esp_root)
        self.openssl = openssl or shutil.which("openssl") or "openssl"
        self._temporary: tempfile.TemporaryDirectory[str] | None = None
        self._listeners: list[socket.socket] = []
        self._threads: list[threading.Thread] = []
        self._stop = threading.Event()
        self._contexts: list[ssl.SSLContext] = []
        self._error: BaseException | None = None

    @property
    def error(self) -> BaseException | None:
        """Unexpected listener failure, if one occurred."""
        return self._error

    def __enter__(self) -> M10TlsPeer:
        return self.start()

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.close()

    def start(self) -> M10TlsPeer:
        if self._threads:
            raise RuntimeError("M10 TLS peer is already running")
        self.esp_root.mkdir(parents=True, exist_ok=True)
        self._temporary = tempfile.TemporaryDirectory(prefix="micant-m10-tls-")
        material = Path(self._temporary.name)
        try:
            self._generate_certificates(material)
            ca_der = material / "root.der"
            subprocess.run(
                [self.openssl, "x509", "-in", str(material / "root.pem"),
                 "-outform", "DER", "-out", str(ca_der)],
                check=True, capture_output=True, text=True,
            )
            encoded_root = ca_der.read_bytes()
            if not encoded_root or len(encoded_root) > 64 * 1024:
                raise RuntimeError("generated M10 root certificate has an invalid size")
            bundle = self.ROOT_BUNDLE_MAGIC + struct.pack("<I", len(encoded_root)) + encoded_root
            (self.esp_root / "ROOT.DER").write_bytes(bundle)

            contexts = [
                self._server_context(material / "server.pem", material / "server.key"),
                self._server_context(material / "rogue-server.pem", material / "rogue-server.key"),
                self._server_context(material / "expired-server.pem", material / "expired-server.key"),
            ]
            listeners: list[socket.socket] = []
            for port in (self.PORT, self.ROGUE_ROOT_PORT, self.EXPIRED_CERTIFICATE_PORT):
                listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind(("0.0.0.0", port))
                listener.listen(8)
                listener.settimeout(0.2)
                listeners.append(listener)
            self._contexts = contexts
            self._listeners = listeners
            self._threads = [
                threading.Thread(target=self._serve, args=(listener, context),
                    name=f"micant-m10-tls-peer-{port}", daemon=True)
                for port, listener, context in zip(
                    (self.PORT, self.ROGUE_ROOT_PORT, self.EXPIRED_CERTIFICATE_PORT),
                    listeners, contexts, strict=True)
            ]
            for thread in self._threads:
                thread.start()
            return self
        except BaseException:
            self.close()
            raise

    def close(self) -> None:
        self._stop.set()
        listeners, self._listeners = self._listeners, []
        for listener in listeners:
            try:
                listener.close()
            except OSError:
                pass
        threads, self._threads = self._threads, []
        for thread in threads:
            if thread is threading.current_thread():
                continue
            thread.join(timeout=7)
            if thread.is_alive():
                raise RuntimeError("M10 TLS peer did not stop within seven seconds")
        temporary, self._temporary = self._temporary, None
        if temporary is not None:
            temporary.cleanup()
        self._contexts = []

    def _generate_certificates(self, directory: Path) -> None:
        root_key = directory / "root.key"
        root_pem = directory / "root.pem"
        server_key = directory / "server.key"
        request = directory / "server.csr"
        server_pem = directory / "server.pem"
        extensions = directory / "server.ext"
        extensions.write_text(
            "subjectAltName=DNS:host\n"
            "basicConstraints=critical,CA:FALSE\n"
            "keyUsage=critical,digitalSignature,keyEncipherment\n"
            "extendedKeyUsage=serverAuth\n",
            encoding="ascii",
        )
        self._openssl(
            "req", "-x509", "-newkey", "rsa:2048", "-sha256", "-nodes",
            "-keyout", str(root_key), "-out", str(root_pem), "-days", "3650",
            "-subj", "/CN=MicaNT M10 Test Root",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign",
        )
        self._openssl(
            "req", "-new", "-newkey", "rsa:2048", "-sha256", "-nodes",
            "-keyout", str(server_key), "-out", str(request),
            "-subj", "/CN=host",
        )
        self._openssl(
            "x509", "-req", "-in", str(request), "-CA", str(root_pem),
            "-CAkey", str(root_key), "-CAcreateserial", "-out", str(server_pem),
            "-days", "825", "-sha256", "-extfile", str(extensions),
        )

        rogue_key = directory / "rogue-root.key"
        rogue_pem = directory / "rogue-root.pem"
        rogue_server_key = directory / "rogue-server.key"
        rogue_request = directory / "rogue-server.csr"
        self._openssl(
            "req", "-x509", "-newkey", "rsa:2048", "-sha256", "-nodes",
            "-keyout", str(rogue_key), "-out", str(rogue_pem), "-days", "3650",
            "-subj", "/CN=MicaNT M10 Rogue Test Root",
            "-addext", "basicConstraints=critical,CA:TRUE",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign",
        )
        self._openssl(
            "req", "-new", "-newkey", "rsa:2048", "-sha256", "-nodes",
            "-keyout", str(rogue_server_key), "-out", str(rogue_request),
            "-subj", "/CN=host",
        )
        self._openssl(
            "x509", "-req", "-in", str(rogue_request), "-CA", str(rogue_pem),
            "-CAkey", str(rogue_key), "-CAcreateserial", "-out",
            str(directory / "rogue-server.pem"), "-days", "825", "-sha256",
            "-extfile", str(extensions),
        )

        expired_key = directory / "expired-server.key"
        expired_request = directory / "expired-server.csr"
        self._openssl(
            "req", "-new", "-newkey", "rsa:2048", "-sha256", "-nodes",
            "-keyout", str(expired_key), "-out", str(expired_request),
            "-subj", "/CN=host",
        )
        self._write_expired_certificate_config(directory, root_pem, root_key)
        self._openssl(
            "ca", "-batch", "-config", str(directory / "expired-ca.cnf"),
            "-extensions", "server_ext", "-startdate", "20200101000000Z",
            "-enddate", "20210101000000Z", "-in", str(expired_request),
            "-out", str(directory / "expired-server.pem"),
        )

    def _write_expired_certificate_config(self, directory: Path,
                                          root_pem: Path, root_key: Path) -> None:
        new_certs = directory / "newcerts"
        new_certs.mkdir()
        (directory / "index.txt").write_text("", encoding="ascii")
        (directory / "serial").write_text("1000\n", encoding="ascii")
        (directory / "expired-ca.cnf").write_text(
            "[ ca ]\n"
            "default_ca = CA_default\n"
            "[ CA_default ]\n"
            f"database = {directory / 'index.txt'}\n"
            f"new_certs_dir = {new_certs}\n"
            f"certificate = {root_pem}\n"
            f"private_key = {root_key}\n"
            f"serial = {directory / 'serial'}\n"
            "default_md = sha256\n"
            "default_days = 825\n"
            "policy = policy_anything\n"
            "unique_subject = no\n"
            "copy_extensions = copy\n"
            "[ policy_anything ]\n"
            "commonName = supplied\n"
            "[ server_ext ]\n"
            "subjectAltName = DNS:host\n"
            "basicConstraints = critical,CA:FALSE\n"
            "keyUsage = critical,digitalSignature,keyEncipherment\n"
            "extendedKeyUsage = serverAuth\n",
            encoding="ascii",
        )

    @staticmethod
    def _server_context(certificate: Path, private_key: Path) -> ssl.SSLContext:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(str(certificate), str(private_key))
        return context

    def _openssl(self, *arguments: str) -> None:
        subprocess.run(
            [self.openssl, *arguments], check=True, capture_output=True, text=True,
        )

    def _serve(self, listener: socket.socket, context: ssl.SSLContext) -> None:
        while not self._stop.is_set():
            try:
                connection, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                if self._stop.is_set():
                    return
                self._error = RuntimeError("M10 TLS listener failed while accepting")
                return
            try:
                with connection:
                    connection.settimeout(5)
                    with context.wrap_socket(connection, server_side=True) as secure:
                        request = self._read_request(secure)
                        if request is None:
                            continue
                        first_line = request.split(b"\r\n", 1)[0]
                        if first_line == b"GET /m10-payload HTTP/1.1":
                            status = b"200 OK"
                            body = self.PAYLOAD
                        else:
                            status = b"404 Not Found"
                            body = b"MICA-M10-NOT-FOUND"
                        headers = (
                            b"HTTP/1.1 " + status + b"\r\n"
                            + b"Content-Type: application/octet-stream\r\n"
                            + b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n"
                            + b"Connection: close\r\n\r\n"
                        )
                        secure.sendall(headers + body)
                        # The native WinHTTP client treats an unauthenticated
                        # TCP EOF as a truncated HTTPS response. Send TLS
                        # close_notify, then let the peer close its half.
                        secure.setblocking(False)
                        try:
                            secure.unwrap()
                        except (ssl.SSLWantReadError, ssl.SSLWantWriteError,
                                BlockingIOError):
                            pass
            except (ssl.SSLError, OSError, TimeoutError):
                # Invalid-hostname and malformed-peer tests are expected to
                # abort TLS before an HTTP request reaches this point.
                continue
            except BaseException as error:
                self._error = error
                return

    def _read_request(self, connection: ssl.SSLSocket) -> bytes | None:
        request = bytearray()
        while b"\r\n\r\n" not in request:
            if len(request) >= self.MAX_REQUEST_BYTES:
                return None
            block = connection.recv(min(1024, self.MAX_REQUEST_BYTES - len(request)))
            if not block:
                return None
            request.extend(block)
        return bytes(request)
