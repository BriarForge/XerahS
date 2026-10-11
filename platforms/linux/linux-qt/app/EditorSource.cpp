#include "EditorSource.h"
#include "RasterSource.h"

#include "image-editor/AnnotationRenderer.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <new>

namespace xerahs::app {

namespace {

using namespace xerahs::editor;

class FileSidecarStore final : public SidecarStore {
public:
  bool exists(const QString &path) override { return QFileInfo::exists(path); }
  bool write(const QString &path, const QByteArray &bytes) override {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(bytes) != bytes.size()) return false;
    return file.commit();
  }
  bool remove(const QString &path) override { return QFile::remove(path); }
};

QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); }

LoadedSource fail(const QString &diagnostic) {
  LoadedSource result;
  result.diagnostic = diagnostic;
  return result;
}

}  // namespace

static LoadedSource loadSourceImpl(const QString &path) {
  const LoadedRaster raster = loadRaster(path);
  if (!raster.diagnostic.isEmpty()) return fail(raster.diagnostic);
  const QByteArray &bytes = raster.fileBytes;
  const QImage &image = raster.image;

  QByteArray normalizedPng;
  QBuffer normalized(&normalizedPng);
  if (!normalized.open(QIODevice::WriteOnly) || !image.save(&normalized, "PNG")) return fail(diagnostic::sourceCorrupt);

  LoadedSource result;
  result.image = image;
  const QString hash = QStringLiteral("sha256:") +
                       QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());

  const SidecarPaths sidecar = sidecarPaths(path, [](const QString &p) { return QFileInfo::exists(p); });
  if (sidecar.readPath) {
    QFile sidecarFile(*sidecar.readPath);
    ParseResult parsed;
    if (sidecarFile.open(QIODevice::ReadOnly)) parsed = readXann(sidecarFile.readAll());
    else parsed.error = diagnostic::documentInvalid;
    // ES-015: a failed sidecar must never become a writable bare-raster session.
    if (!parsed.document) return fail(parsed.error.value_or(diagnostic::documentInvalid));
    QImage embedded;
    if (!embedded.loadFromData(parsed.document->sourceImagePng, "PNG")) return fail(diagnostic::documentInvalid);
    embedded = embedded.convertToFormat(QImage::Format_ARGB32);
    if (embedded.isNull()) return fail(diagnostic::documentTooLarge);
    const auto binding = bindSource(*parsed.document, normalizedPng, path, hash);
    if (binding.error) return fail(*binding.error);
    if (binding.choiceRequired) {
      LoadedSource pending;
      pending.pendingChoice = PendingSourceChoice{*parsed.document, image, embedded, normalizedPng, path, hash, *sidecar.readPath};
      return pending;
    }
    result.document = binding.document;
    result.image = embedded;
    result.sidecarPath = *sidecar.readPath;
    return result;
  }

  AnnotationDocument document;
  document.imagePath = path;
  document.imageHash = hash;
  document.canvasWidth = image.width();
  document.canvasHeight = image.height();
  document.createdAt = document.modifiedAt = now();
  document.sourceImagePng = normalizedPng;
  result.document = document;
  return result;
}

LoadedSource loadSource(const QString &path) {
  try { return loadSourceImpl(path); }
  catch (const std::bad_alloc &) { return fail(diagnostic::documentTooLarge); }
}

LoadedSource resolveSourceChoice(const LoadedSource &pending, SourceChoice choice) {
  if (!pending.pendingChoice) return fail(diagnostic::documentInvalid);
  const PendingSourceChoice &sources = *pending.pendingChoice;
  const auto binding = bindSource(sources.document, sources.currentRasterPng, sources.rasterPath, sources.rasterHash, choice);
  if (binding.error) return fail(*binding.error);
  if (binding.choiceRequired) return pending;
  LoadedSource result;
  result.document = binding.document;
  result.image = choice == SourceChoice::CurrentRaster ? sources.currentRaster : sources.embeddedSource;
  result.sidecarPath = sources.sidecarPath;
  return result;
}

SaveOutcome saveEdit(const QString &rasterPath, const QImage &source, const AnnotationDocument &document) {
  SaveOutcome outcome;
  const QImage argb = source.convertToFormat(QImage::Format_ARGB32);
  ArgbImage input;
  input.width = argb.width();
  input.height = argb.height();
  input.pixels.resize(static_cast<std::size_t>(input.width * input.height));
  for (int y = 0; y < argb.height(); ++y) {
    const auto *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
    for (int x = 0; x < argb.width(); ++x) input.pixels[static_cast<std::size_t>(y) * input.width + x] = row[x];
  }
  const RenderResult rendered = render(input, document.annotations);
  if (!rendered.image) {
    outcome.rasterError = QStringLiteral("The image or annotation geometry is invalid and cannot be exported.");
    return outcome;
  }
  QImage out(argb.size(), QImage::Format_ARGB32);
  out.setColorSpace(source.colorSpace());
  for (int y = 0; y < out.height(); ++y) {
    auto *row = reinterpret_cast<QRgb *>(out.scanLine(y));
    for (int x = 0; x < out.width(); ++x) row[x] = rendered.image->at(x, y);
  }
  QSaveFile raster(rasterPath);
  if (raster.open(QIODevice::WriteOnly) && out.save(&raster, "PNG") && raster.commit()) {
    outcome.rasterSaved = true;
  } else {
    outcome.rasterError = raster.errorString();
    return outcome;
  }

  // The sidecar embeds the unannotated source, so the saved raster can be
  // re-edited without compounding annotations.
  AnnotationDocument saved = document;
  saved.imagePath = rasterPath;
  QFile written(rasterPath);
  QByteArray writtenBytes;
  if (written.open(QIODevice::ReadOnly)) writtenBytes = written.readAll();
  saved.imageHash = QStringLiteral("sha256:") +
                    QString::fromLatin1(QCryptographicHash::hash(writtenBytes, QCryptographicHash::Sha256).toHex());
  saved.modifiedAt = now();
  FileSidecarStore store;
  outcome.sidecarSaved = saveSidecar(store, rasterPath, saved, true).ok;
  return outcome;
}

}  // namespace xerahs::app
