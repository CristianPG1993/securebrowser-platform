"""Servidor local para probar el cierre de SecureBrowser durante una carga.

Desde la raíz: python desktop/tests/manual/serve-slow-load.py
Abrir en SecureBrowser: http://127.0.0.1:8766/slow.html
"""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import time
from urllib.parse import urlsplit


class SlowLoadHandler(BaseHTTPRequestHandler):
    """Entrega una página inmediatamente y retiene su imagen durante 20 segundos."""

    def do_GET(self):
        path = urlsplit(self.path).path
        if path in ("/", "/slow.html"):
            self.send_page()
        elif path == "/slow-resource.svg":
            self.send_slow_resource()
        else:
            self.send_error(404)

    def send_page(self):
        # El texto aparece pronto, pero la imagen pendiente mantiene abierto el ciclo de carga.
        page = """<!doctype html>
<html lang="es">
<head>
    <meta charset="utf-8">
    <title>Prueba de cierre durante carga</title>
    <link rel="icon" href="data:,">
</head>
<body>
    <h1>Cierre durante carga</h1>
    <p id="state">Recurso pendiente: pulsa la X de SecureBrowser antes de que termine la carga.</p>
    <img src="/slow-resource.svg" width="320" height="80" alt="Imagen pendiente de carga">
    <script>
        // El evento load llegará cuando termine también la imagen retenida por el servidor.
        function showCompleted() {
            document.getElementById("state").textContent =
                "Carga completada. Recarga la página para repetir la prueba durante la espera.";
        }
        window.addEventListener("load", showCompleted);
    </script>
</body>
</html>
"""
        self.send_body(page.encode("utf-8"), "text/html; charset=utf-8")

    def send_slow_resource(self):
        # La pausa solo afecta a este recurso del servidor de prueba; CEF y Qt siguen ejecutándose.
        print(
            "Recurso solicitado: esperando 20 segundos. Puedes cerrar SecureBrowser ahora.",
            flush=True,
        )
        time.sleep(20)
        image = (
            b'<svg xmlns="http://www.w3.org/2000/svg" width="320" height="80">'
            b'<rect width="320" height="80" fill="green"/></svg>'
        )
        self.send_body(image, "image/svg+xml")

    def send_body(self, body, content_type):
        try:
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            # Cada visita debe volver a solicitar la imagen, aunque ya se haya probado antes.
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)
        except ConnectionError:
            # Cerrar el navegador durante la espera puede interrumpir esta conexión.
            print("Conexión del navegador interrumpida.", flush=True)


if __name__ == "__main__":
    # Solo escucha en este equipo. Ctrl+C detiene el servidor manual.
    with ThreadingHTTPServer(("127.0.0.1", 8766), SlowLoadHandler) as server:
        print("Página de prueba: http://127.0.0.1:8766/slow.html", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
