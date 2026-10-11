#include "EditorOpen.h"
#include <QDialog>
#include <QEventLoop>
#include <QImageReader>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <atomic>
#include <future>

namespace xerahs::app {
using namespace xerahs::editor;
namespace {
LoadedSource failure(const QString &diagnostic) { LoadedSource result; result.diagnostic = diagnostic; return result; }
LoadedSource runWork(const std::function<LoadedSource(const RenderControl &)> &work, QWidget *parent) {
  std::atomic<bool> cancelled{false}; std::atomic<int> progress{0};
  QProgressDialog dialog(QStringLiteral("Opening image…"), QStringLiteral("Cancel"), 0, 100, parent);
  dialog.setObjectName(QStringLiteral("openImageProgress"));
  dialog.setAccessibleName(QStringLiteral("Open image progress"));
  dialog.setWindowTitle(QStringLiteral("Open Image")); dialog.setWindowModality(Qt::WindowModal);
  dialog.setAutoClose(false); dialog.setAutoReset(false);
  std::future<LoadedSource> worker;
  try {
    worker = std::async(std::launch::async, [&] {
      return work({[&] { return cancelled.load(); }, [&](int p) { progress.store(p); }});
    });
  } catch (const std::exception &) { return failure(diagnostic::documentTooLarge); }
  QEventLoop completion;
  QObject::connect(&dialog, &QProgressDialog::canceled, &completion, [&] {
    cancelled.store(true); dialog.setLabelText(QStringLiteral("Cancelling image opening…")); dialog.setCancelButton(nullptr);
  });
  QObject::connect(&dialog, &QDialog::rejected, &completion, [&] { cancelled.store(true); });
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, &dialog, [&] {
    dialog.setValue(progress.load());
    if (worker.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) { dialog.accept(); completion.quit(); }
  });
  timer.start(20); dialog.open(); completion.exec(); timer.stop();
  // A hidden/cancelled progress dialog cannot release worker-owned references.
  // Consume the same worker only after it is terminal, then publish atomically.
  try {
    auto result = worker.get();
    return cancelled.load() ? failure(QStringLiteral("canvas-cancelled")) : result;
  } catch (const std::bad_alloc &) { return failure(diagnostic::documentTooLarge); }
    catch (const std::exception &) { return failure(diagnostic::documentInvalid); }
}
}  // namespace
QString imageFileFilter() {
  QStringList patterns;
  for (const auto &format : QImageReader::supportedImageFormats()) patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
  return QStringLiteral("Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
}
LoadedSource openSource(const QString &path, QWidget *parent) {
  auto source = runWork([path](const RenderControl &control) { return loadSource(path, control); }, parent);
  if (!source.pendingChoice) return source;
  const auto &sources = *source.pendingChoice;
  QMessageBox box(QMessageBox::Warning, QStringLiteral("Image changed since annotations were saved"),
      QStringLiteral("Choose the image to use with the saved annotations. Their coordinates will be kept. "
                     "Cancel leaves both files unchanged."), QMessageBox::Cancel, parent);
  auto *current = box.addButton(QStringLiteral("Current raster (%1 × %2)").arg(sources.currentRaster.width()).arg(sources.currentRaster.height()), QMessageBox::ActionRole);
  auto *embedded = box.addButton(QStringLiteral("Embedded source (%1 × %2)").arg(sources.embeddedSource.width()).arg(sources.embeddedSource.height()), QMessageBox::ActionRole);
  box.setDefaultButton(QMessageBox::Cancel); box.exec();
  const auto choice = box.clickedButton() == current ? SourceChoice::CurrentRaster :
      box.clickedButton() == embedded ? SourceChoice::EmbeddedSource : SourceChoice::Unspecified;
  if (choice == SourceChoice::Unspecified) return failure(QStringLiteral("canvas-cancelled"));
  return runWork([source, choice](const RenderControl &control) { return resolveSourceChoice(source, choice, control); }, parent);
}
}  // namespace xerahs::app
