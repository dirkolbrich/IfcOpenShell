#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QQmlApplicationEngine engine;
  QQuickStyle::setStyle("Basic");

  // load the main.qml file into the QML engine
  engine.addImportPath(":/ifcviewer/ui");
  engine.loadFromModule("IfcViewer.Ui", "Main");

  return app.exec();
}