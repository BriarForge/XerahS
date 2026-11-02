#include "CapturePipeline.h"
#include <QApplication>
#include <QThread>
#include <cstdio>
using namespace xerahs::app;
int main(int argc, char **argv) {
  QApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
  if (argc != 4) return 2;
  auto journal = std::make_shared<NativePipelineJournal>(QString::fromLocal8Bit(argv[1]));
  CaptureActionPorts ports;
  const auto checkpoint = [](const QString &) { std::puts("checkpoint"); std::fflush(stdout); QThread::sleep(60); };
  if (QString::fromLocal8Bit(argv[3]) == "before-move") ports.deletion.beforeMove = checkpoint;
  else ports.deletion.afterMove = checkpoint;
  QImage image(12, 8, QImage::Format_ARGB32); image.fill(Qt::green);
  CapturePipeline pipeline(image, QString::fromLocal8Bit(argv[2]), "capture", {"save", "delete"}, nullptr, ports, journal);
  QObject::connect(&pipeline, &CapturePipeline::finished, &app, &QCoreApplication::quit);
  pipeline.start(); return app.exec();
}
