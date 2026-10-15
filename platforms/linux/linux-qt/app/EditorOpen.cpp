#include "EditorOpen.h"
#include "EditorPreparation.h"
#include <QDir>
#include <QImageReader>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <memory>

namespace xerahs::app {
using namespace xerahs::editor;
namespace {
LoadedSource failure(const QString &diagnostic) { LoadedSource result; result.diagnostic = diagnostic; return result; }
LoadedSource runWork(std::function<LoadedSource(const RenderControl &)> operation, QWidget *parent, EditorOpenHooks hooks) {
  try {
    const auto source = std::make_shared<LoadedSource>();
    const auto result = runEditorPreparation(parent, {"openImageProgress", "Open Image", "Opening image…", "Open image progress"},
        [operation = std::move(operation), hooks, source](const RenderControl &control) {
          try { if (hooks.beforePrepare) hooks.beforePrepare(); *source = operation(control); }
          catch (const std::bad_alloc &) { *source = failure(diagnostic::documentTooLarge); }
          catch (...) { *source = failure(diagnostic::documentInvalid); }
        });
    if (result == EditorPreparationResult::Ready) return std::move(*source);
    return failure(result == EditorPreparationResult::Cancelled ? QStringLiteral("canvas-cancelled") : diagnostic::documentInvalid);
  } catch (...) { return failure(diagnostic::documentTooLarge); }
}
void dispose(QPointer<QDialog> dialog) {
  if (dialog) dialog->hide();
  if (dialog) dialog->setParent(nullptr);
  delete dialog.data();
}
}  // namespace
QString imageFileFilter() {
  QStringList patterns;
  for (const auto &format : QImageReader::supportedImageFormats()) patterns.append(QStringLiteral("*.%1").arg(QString::fromLatin1(format)));
  return QStringLiteral("Images (%1);;All files (*)").arg(patterns.join(QLatin1Char(' ')));
}
QString absoluteImagePath(const QString &path) {
  if (path.isEmpty() || QDir::isAbsolutePath(path)) return path;
  return QDir::currentPath() + QLatin1Char('/') + path;
}
LoadedSource openSource(const QString &path, QWidget *parent, const EditorOpenHooks &hooks) {
  const QPointer<QWidget> owner(parent);
  auto source = runWork([path](const RenderControl &control) { return loadSource(path, control); }, parent, hooks);
  if ((parent && !owner) || source.diagnostic == u"canvas-cancelled") return failure(QStringLiteral("canvas-cancelled"));
  if (!source.pendingChoice) return source;
  const auto sources = *source.pendingChoice;
  QPointer<QMessageBox> box(new QMessageBox(QMessageBox::Warning, QStringLiteral("Image changed since annotations were saved"),
      QStringLiteral("Choose the image to use with the saved annotations. Their coordinates will be kept. "
                     "Cancel leaves both files unchanged."), QMessageBox::Cancel, parent));
  box->setObjectName("openImageSourceChoice"); box->setTextFormat(Qt::PlainText);
  auto *current = box->addButton(QStringLiteral("Current raster (%1 × %2)").arg(sources.currentRaster.width()).arg(sources.currentRaster.height()), QMessageBox::ActionRole);
  auto *embedded = box->addButton(QStringLiteral("Embedded source (%1 × %2)").arg(sources.embeddedSource.width()).arg(sources.embeddedSource.height()), QMessageBox::ActionRole);
  box->setDefaultButton(QMessageBox::Cancel); box->exec();
  const auto choice = box && box->clickedButton() == current ? SourceChoice::CurrentRaster :
      box && box->clickedButton() == embedded ? SourceChoice::EmbeddedSource : SourceChoice::Unspecified;
  dispose(box.data());
  if ((parent && !owner) || choice == SourceChoice::Unspecified) return failure(QStringLiteral("canvas-cancelled"));
  return runWork([source, choice](const RenderControl &control) { return resolveSourceChoice(source, choice, control); }, parent, hooks);
}
LoadedSource openCapturedSource(const QImage &image, QWidget *parent, const EditorOpenHooks &hooks) {
  // A QImage may wrap caller-owned bytes. Bound and own that ingress before a
  // cancellable native wait can return and release the caller's backing buffer.
  if (image.isNull()) return failure(diagnostic::sourceEmpty);
  if (image.width() > kMaxDimension || image.height() > kMaxDimension || qint64(image.width()) * image.height() > kMaxPixels)
    return failure(diagnostic::sourceTooLarge);
  try {
    const auto owned = image.copy();
    if (owned.isNull()) return failure(diagnostic::sourceTooLarge);
    return runWork([owned](const RenderControl &control) { return prepareCapturedSource(owned, control); }, parent, hooks);
  } catch (...) { return failure(diagnostic::sourceTooLarge); }
}
LoadedSource openCapturedDocument(const AnnotationDocument &document, QWidget *parent, const EditorOpenHooks &hooks) {
  return runWork([document](const RenderControl &control) { return prepareCapturedDocument(document, control); }, parent, hooks);
}
}  // namespace xerahs::app
