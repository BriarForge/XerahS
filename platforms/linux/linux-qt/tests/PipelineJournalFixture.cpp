#include "CapturePipeline.h"
#include "ActionSelectionDialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QThread>
#include <QTimer>
#include <cstdio>
using namespace xerahs::app;
using namespace xerahs::actions;
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false); // production tray lifetime, including after selector acceptance
  if (argc != 4) return 2;
  auto journal = std::make_shared<NativePipelineJournal>(QString::fromLocal8Bit(argv[1]));
  const auto mode = QString::fromLocal8Bit(argv[3]);
  const auto stop = [] { std::puts("checkpoint"); std::fflush(stdout); QThread::sleep(60); };
  if (mode.startsWith("retry-")) {
    CaptureActionPorts initial;
    if (mode == "retry-before-publication") initial.save.encode = [](const QImage &, QIODevice *) { return false; };
    initial.clipboard = [mode](const QImage &, const CancellationToken &) {
      return mode == "retry-clipboard" ? ActionOutcome{ActionState::Failed, "clipboard-unavailable", {}} : ActionOutcome{};
    };
    QImage image(9, 7, QImage::Format_ARGB32); image.fill(Qt::red);
    CapturePipeline original(image, QString::fromLocal8Bit(argv[2]), "capture", {"save", "image-clipboard"}, nullptr, initial, journal);
    QObject::connect(&original, &CapturePipeline::finished, &app, [&](const CapturePipelineReceipt &source) {
      CaptureActionPorts ports;
      if (mode == "retry-before-publication") ports.save.beforePublish = [stop](const QString &) { stop(); };
      ports.clipboard = [mode, stop](const QImage &, const CancellationToken &) { if (mode == "retry-clipboard") stop(); return ActionOutcome{}; };
      auto *retry = new CapturePipeline(source, &app, ports, journal);
      QObject::connect(retry, &CapturePipeline::finished, &app, &QCoreApplication::quit); retry->start();
    });
    original.start(); return app.exec();
  }
  CaptureActionPorts ports;
  if (mode == "save-as-before-publication" || mode == "both-save-as-before-publication") {
    ports.chooseSaveAs = [directory = QString::fromLocal8Bit(argv[2])](const QString &, const CancellationToken &) {
      return CaptureSaveAsChoice{directory + "/chosen.png", {}};
    };
    ports.save.beforePublish = [stop](const QString &path) { if (path.endsWith("/chosen.png")) stop(); };
  }
  if (mode == "file-output") ports.fileOutput = [stop](const QString &, const CaptureFile &, const CancellationToken &) { stop(); return ActionOutcome{}; };
  if (mode == "before-publication" || mode == "selection-before-publication") ports.save.beforePublish = [stop](const QString &) { stop(); };
  ports.clipboard = [mode, stop](const QImage &, const CancellationToken &) { if (mode == "after-save") stop(); return ActionOutcome{}; };
  QImage image(9, 7, QImage::Format_ARGB32); image.fill(Qt::red);
  QStringList selected{"save", "image-clipboard"};
  if (mode == "save-as-before-publication") selected = {"save-as"};
  if (mode == "both-save-as-before-publication") selected = {"save", "save-as"};
  if (mode == "save-as-before-publication") QDir().mkpath(QString::fromLocal8Bit(argv[2]));
  if (mode == "file-output") selected = {"save", "file-clipboard"};
  QTimer selection;
  if (mode == "selection-before-publication") {
    selected.prepend("after-capture-window"); selection.setInterval(10);
    QObject::connect(&selection, &QTimer::timeout, &app, [&] {
      auto *dialog = qobject_cast<ActionSelectionDialog *>(QApplication::activeModalWidget()); if (!dialog) return;
      dialog->findChild<QCheckBox *>("capture-action-image-clipboard")->setChecked(false); selection.stop(); dialog->accept();
    }); selection.start();
  }
  CapturePipeline pipeline(image, QString::fromLocal8Bit(argv[2]), "capture", selected, nullptr, ports, journal);
  QObject::connect(&pipeline, &CapturePipeline::finished, &app, &QCoreApplication::quit);
  pipeline.start(); return app.exec();
}
