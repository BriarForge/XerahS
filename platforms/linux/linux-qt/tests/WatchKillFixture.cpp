// CORE-025 P7: the helper process for the real SIGKILL proof.
//
// The helper watches a directory, uploads the file it finds to a local
// destination, and stays alive while the request is in flight. The parent
// test kills it with SIGKILL once the server has received the first request,
// then starts it again against the same store. The restarted process must not
// send a second request, and must report the publication as unconfirmed.
//
// Usage: watch-kill-fixture <input-dir> <store-dir> <destination-url>
// It prints "ready" on stdout once the upload has been dispatched.
#include "FileUploadController.h"
#include "NativeWorkflowExecutions.h"
#include "WatchFolderService.h"
#include "history/CaptureHistory.h"
#include "uploads/DestinationCatalog.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <QWidget>
#include <cstdio>

using namespace xerahs;
using namespace xerahs::app;
using namespace xerahs::uploads;

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  if (argc < 4) {
    std::fputs("usage: watch-kill-fixture <input-dir> <store-dir> <destination-url>\n", stderr);
    return 2;
  }
  const QString input = QString::fromLocal8Bit(argv[1]);
  const QString store = QString::fromLocal8Bit(argv[2]);
  const QString url = QString::fromLocal8Bit(argv[3]);

  FileUploadPorts ports;
  ports.catalogPath = QDir(store).filePath("destinations.json");
  ports.history = std::make_shared<NativePipelineJournal>(QDir(store).filePath("history"));
  ports.executions = NativeWorkflowExecutions(QDir(store).filePath("executions")).writer();
  ports.credentials = std::make_shared<MemoryCredentialStore>();

  DestinationCatalog catalog(ports.catalogPath);
  UploadDestination destination;
  destination.id = QUuid::createUuid();
  destination.name = QStringLiteral("Watch kill fixture");
  destination.requestUrl = url;
  destination.body = BodyType::MultipartFormData;
  destination.fileFormName = QStringLiteral("file");
  destination.resultUrl = QStringLiteral("{json:url}");
  if (!catalog.add(destination).isEmpty()) {
    std::fputs("fixture: cannot write the destination catalog\n", stderr);
    return 3;
  }

  QWidget owner;
  FileUploadController uploads(ports, &owner);
  watch::WatchFolders folders(store);
  WatchFolderService service(folders, uploads, [&] { return QDir(store).filePath("captures"); },
                             [] { return QDateTime::currentMSecsSinceEpoch(); });
  watch::Rule rule;
  rule.path = input;
  if (!service.setRules({rule}).isEmpty()) {
    std::fputs("fixture: cannot write the watch rules\n", stderr);
    return 4;
  }
  QObject::connect(&service, &WatchFolderService::notify, [](const QString &message, bool) {
    std::fputs(qPrintable(message + QLatin1Char('\n')), stdout);
    std::fflush(stdout);
  });
  QObject::connect(&service, &WatchFolderService::processed, [](const QString &path, bool success) {
    std::fprintf(stdout, "processed %s %d\n", qPrintable(path), int(success));
    std::fflush(stdout);
  });
  service.start();
  // The parent kills this process once the server has the first request.
  QTimer::singleShot(30000, &app, [] { QCoreApplication::quit(); });
  return app.exec();
}
