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
#include <algorithm>
#include <cstring>

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

bool cancelled(const RenderControl &control) { return control.cancelled && control.cancelled(); }
void progress(const RenderControl &control, int value) { if (control.progress) control.progress(value); }
QImage displayCopy(const QImage &image) {
  QImage tagged = image;
  if (!tagged.colorSpace().isValid()) tagged.setColorSpace(QColorSpace::SRgb);
  return tagged.colorSpace() == QColorSpace(QColorSpace::SRgb) ? tagged : tagged.convertedToColorSpace(QColorSpace::SRgb);
}

}  // namespace

LoadedSource prepareCapturedSource(const QImage &image, const RenderControl &control) {
  try {
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    if (image.isNull()) return fail(diagnostic::sourceEmpty);
    if (image.width() > kMaxDimension || image.height() > kMaxDimension || qint64(image.width()) * image.height() > kMaxPixels)
      return fail(diagnostic::sourceTooLarge);
    const auto normalized = image.convertToFormat(QImage::Format_ARGB32);
    if (normalized.isNull()) return fail(diagnostic::sourceTooLarge);
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    QImage source(image.width(), image.height(), QImage::Format_ARGB32);
    if (source.isNull()) return fail(diagnostic::sourceTooLarge);
    source.setColorSpace(image.colorSpace());
    for (int row = 0; row < source.height(); ++row) {
      if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
      std::memcpy(source.scanLine(row), normalized.constScanLine(row), std::size_t(source.width()) * 4);
      progress(control, (row + 1) * 60 / source.height());
    }
    const auto display = displayCopy(source);
    if (display.isNull()) return fail(diagnostic::sourceTooLarge);
    progress(control, 70);
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    AnnotationDocument document;
    document.canvasWidth = source.width(); document.canvasHeight = source.height();
    document.createdAt = document.modifiedAt = now();
    QBuffer buffer(&document.sourceImagePng);
    if (!buffer.open(QIODevice::WriteOnly) || !source.save(&buffer, "PNG")) return fail(diagnostic::sourceCorrupt);
    if (document.sourceImagePng.size() > kMaxEncodedImageBytes) return fail(diagnostic::sourceTooLarge);
    progress(control, 90);
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    const auto parsed = parseDocument(serializeDocument(document));
    if (!parsed.document) return fail(parsed.error.value_or(diagnostic::documentInvalid));
    progress(control, 100);
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    LoadedSource result; result.document = std::move(document); result.image = std::move(source); result.displayImage = display;
    return result;
  } catch (const std::bad_alloc &) { return fail(diagnostic::sourceTooLarge); }
    catch (...) { return fail(diagnostic::documentInvalid); }
}

static LoadedSource loadSourceImpl(const QString &path, const RenderControl &control) {
  const LoadedRaster raster = loadRaster(path, {control.cancelled, [&](int p) { progress(control, p * 45 / 100); }});
  if (!raster.diagnostic.isEmpty()) return fail(raster.diagnostic);
  const QByteArray &bytes = raster.fileBytes;
  const QImage &image = raster.image;

  QByteArray normalizedPng;
  QBuffer normalized(&normalizedPng);
  if (!normalized.open(QIODevice::WriteOnly) || !image.save(&normalized, "PNG")) return fail(diagnostic::sourceCorrupt);
  progress(control, 50);
  if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));

  LoadedSource result;
  result.image = image;
  QCryptographicHash digest(QCryptographicHash::Sha256);
  for (qsizetype offset = 0; offset < bytes.size(); offset += 65536) {
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    digest.addData(QByteArrayView(bytes.constData() + offset, std::min(qsizetype(65536), bytes.size() - offset)));
    progress(control, 50 + int(double(offset) / bytes.size() * 10));
  }
  const QString hash = QStringLiteral("sha256:") + QString::fromLatin1(digest.result().toHex());
  progress(control, 60);
  if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));

  const SidecarPaths sidecar = sidecarPaths(path, [](const QString &p) { return QFileInfo::exists(p); });
  if (sidecar.readPath) {
    QFile sidecarFile(*sidecar.readPath);
    ParseResult parsed;
    if (!sidecarFile.open(QIODevice::ReadOnly)) return fail(diagnostic::documentInvalid);
    if (sidecarFile.size() > kMaxEncodedImageBytes) return fail(diagnostic::documentTooLarge);
    QByteArray sidecarBytes;
    while (!sidecarFile.atEnd()) {
      if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
      const auto chunk = sidecarFile.read(65536);
      if (sidecarFile.error() != QFile::NoError) return fail(diagnostic::documentInvalid);
      if (sidecarBytes.size() + chunk.size() > kMaxEncodedImageBytes) return fail(diagnostic::documentTooLarge);
      sidecarBytes.append(chunk);
    }
    parsed = readXann(sidecarBytes);
    progress(control, 75);
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    // ES-015: a failed sidecar must never become a writable bare-raster session.
    if (!parsed.document) return fail(parsed.error.value_or(diagnostic::documentInvalid));
    QImage embedded;
    if (!embedded.loadFromData(parsed.document->sourceImagePng, "PNG")) return fail(diagnostic::documentInvalid);
    embedded = embedded.convertToFormat(QImage::Format_ARGB32);
    if (embedded.isNull()) return fail(diagnostic::documentTooLarge);
    progress(control, 80);
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
    const auto binding = bindSource(*parsed.document, normalizedPng, path, hash);
    if (binding.error) return fail(*binding.error);
    const QImage embeddedDisplay = displayCopy(embedded);
    if (embeddedDisplay.isNull()) return fail(diagnostic::documentTooLarge);
    if (binding.choiceRequired) {
      const QImage currentDisplay = displayCopy(image);
      if (currentDisplay.isNull()) return fail(diagnostic::documentTooLarge);
      progress(control, 100);
      if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
      LoadedSource pending;
      pending.pendingChoice = PendingSourceChoice{*parsed.document, image, embedded, currentDisplay, embeddedDisplay,
          normalizedPng, path, hash, *sidecar.readPath};
      return pending;
    }
    result.document = binding.document;
    result.image = embedded;
    result.displayImage = embeddedDisplay;
    result.sidecarPath = *sidecar.readPath;
    progress(control, 100);
    if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
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
  result.displayImage = displayCopy(image);
  if (result.displayImage.isNull()) return fail(diagnostic::documentTooLarge);
  progress(control, 100);
  if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
  return result;
}

LoadedSource loadSource(const QString &path, const RenderControl &control) {
  try { return loadSourceImpl(path, control); }
  catch (const std::bad_alloc &) { return fail(diagnostic::documentTooLarge); }
}

LoadedSource resolveSourceChoice(const LoadedSource &pending, SourceChoice choice, const RenderControl &control) {
  if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
  if (!pending.pendingChoice) return fail(diagnostic::documentInvalid);
  const PendingSourceChoice &sources = *pending.pendingChoice;
  const auto binding = bindSource(sources.document, sources.currentRasterPng, sources.rasterPath, sources.rasterHash, choice);
  if (binding.error) return fail(*binding.error);
  progress(control, 100);
  if (cancelled(control)) return fail(QStringLiteral("canvas-cancelled"));
  if (binding.choiceRequired) return pending;
  LoadedSource result;
  result.document = binding.document;
  result.image = choice == SourceChoice::CurrentRaster ? sources.currentRaster : sources.embeddedSource;
  result.displayImage = choice == SourceChoice::CurrentRaster ? sources.currentDisplay : sources.embeddedDisplay;
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
