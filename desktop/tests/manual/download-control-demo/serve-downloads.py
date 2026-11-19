"""Servidor de archivos inertes para comprobar manualmente Download Control y Qt.

Desde la raíz: python -u desktop/tests/manual/download-control-demo/serve-downloads.py
Abrir en SecureBrowser: http://downloads.test:8767/ con el mapeo indicado en README.md.
"""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import time
from urllib.parse import quote, urlsplit


class DownloadDemoHandler(BaseHTTPRequestHandler):
    """Sirve únicamente la página y las descargas controladas, sin listar directorios."""

    # Chromium solo reconoce validadores fuertes como ETag a partir de HTTP/1.1.
    protocol_version = "HTTP/1.1"

    # La extensión de la URL contradice a propósito algunos nombres de Content-Disposition.
    FILES = {
        "/download/allowed.exe": ("informe.pdf", False),
        "/download/blocked.pdf": ("programa.exe", False),
        "/download/installer.pdf": ("paquete.msi", False),
        "/download/unicode": ("informe-ni\u00f1o.PDF", False),
        "/download/slow.pdf": ("lento.pdf", True),
    }
    PAYLOAD = b"SBP manual download fixture.\n"

    def do_GET(self):
        path = urlsplit(self.path).path
        try:
            if path in ("/", "/index.html"):
                page = Path(__file__).with_name("index.html").read_bytes()
                self.send_headers("text/html; charset=utf-8", len(page))
                self.wfile.write(page)
            elif path == "/download/interrupted.pdf":
                self.send_incomplete_attachment()
            elif path in self.FILES:
                name, slow = self.FILES[path]
                self.send_attachment(name, slow)
            else:
                self.send_error(404)
        except ConnectionError:
            # Cancelar o cerrar SecureBrowser puede interrumpir una respuesta HTTP en curso.
            print("El cliente ha cerrado la conexión de descarga.", flush=True)

    def send_headers(self, content_type, size, disposition=None, etag=None):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(size))
        self.send_header("Cache-Control", "no-store")
        if disposition is not None:
            self.send_header("Content-Disposition", disposition)
        if etag is not None:
            self.send_header("ETag", etag)
        self.end_headers()

    def send_incomplete_attachment(self):
        # Un ETag fuerte permite a Chromium detectar como error la longitud anunciada que no se entrega.
        self.send_headers(
            "application/octet-stream",
            len(self.PAYLOAD) + 1024,
            'attachment; filename="interrumpido.pdf"',
            '"sbp-manual-incomplete"',
        )
        self.wfile.write(self.PAYLOAD)
        self.wfile.flush()
        self.close_connection = True
        print("Fallo de prueba: conexión cerrada antes de entregar el contenido completo.", flush=True)

    def send_attachment(self, name, slow):
        # filename* conserva el nombre Unicode; el cuerpo siempre contiene texto fijo de prueba.
        disposition = "attachment; filename*=UTF-8''" + quote(name, safe="")
        chunk = self.PAYLOAD * 1024 if slow else self.PAYLOAD
        parts = 120 if slow else 1
        self.send_headers("application/octet-stream", len(chunk) * parts, disposition)
        if slow:
            print("Descarga lenta iniciada: entrega en bloques durante unos 30 segundos.", flush=True)
        for index in range(parts):
            self.wfile.write(chunk)
            self.wfile.flush()
            if slow and index + 1 < parts:
                # Solo espera este servidor manual; el hilo de Qt y el de CEF siguen disponibles.
                time.sleep(0.25)
        if slow:
            print("Descarga lenta: respuesta HTTP entregada por completo.", flush=True)


if __name__ == "__main__":
    # Escucha solo en IPv4 loopback; Ctrl+C detiene la herramienta de desarrollo.
    with ThreadingHTTPServer(("127.0.0.1", 8767), DownloadDemoHandler) as server:
        print("Página de descargas: http://downloads.test:8767/ (usar el mapeo de README.md)", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
