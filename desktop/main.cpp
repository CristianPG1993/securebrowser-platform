#include <QApplication>
#include <QWidget>

int main(int argc, char* argv[]) {
    // Inicializa Qt Widgets y procesa sus opciones de arranque.
    // Debe existir antes de crear cualquier widget.
    QApplication app(argc, argv);

    // Un QWidget sin padre actúa como una ventana independiente.
    QWidget window;
    window.setWindowTitle("SecureBrowser");
    window.resize(1024, 768);
    window.show();

    // Inicia el bucle que atiende eventos de ratón, teclado y ventana.
    // Al cerrar la última ventana, termina y devuelve el código de salida.
    return app.exec();
}
