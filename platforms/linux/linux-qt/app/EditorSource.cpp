#include "EditorSource.h"

#include "image-editor/AnnotationRenderer.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>

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

LoadedSource loadSource(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return fail(diagnostic::sourceUnsupported);
  const QByteArray bytes = file.readAll();
  if (bytes.isEmpty()) return fail(diagnostic::sourceEmpty);

  // ES-001: size limits are checked from the header before pixels are allocated.
  QBuffer buffer;
  buffer.setData(bytes);
  buffer.open(QIODevice::ReadOnly);
  QImageReader reader(&buffer);
  reader.setAutoTransform(true);  // ES-002: orientation normalized once
  if (!reader.canRead()) return fail(diagnostic::sourceUnsupported);
  const QSize size = reader.size();
  if (size.isValid()) {
    if (size.width() < 1 || size.height() < 1) return fail(diagnostic::sourceEmpty);
    if (size.width() > kMaxDimension || size.height() > kMaxDimension ||
        static_cast<qint64>(size.width()) * size.height() > kMaxPixels) {
      return fail(diagnostic::sourceTooLarge);
    }
  }
  QImage image = reader.read();
  if (image.isNull()) return fail(diagnostic::sourceCorrupt);
  if (image.width() > kMaxDimension || image.height() > kMaxDimension ||
      static_cast<qint64>(image.width()) * image.height() > kMaxPixels) {
    return fail(diagnostic::sourceTooLarge);
  }
  image = image.convertToFormat(QImage::Format_ARGB32);

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
    if (parsed.document) {
      AnnotationDocument document = *parsed.document;
      // ES-016: never silently bind annotations to mismatched pixels.
      if (!document.imageHash.isEmpty() && document.imageHash != hash) {
        result.warnings << QStringLiteral(
            "The image changed since these annotations were saved. They are kept on the current image.");
      } else if (!document.sourceImagePng.isEmpty()) {
        // The raster is the flattened export this sidecar was saved with: edit
        // the embedded unannotated source so annotations are not applied twice.
        QImage embedded;
        if (embedded.loadFromData(document.sourceImagePng, "PNG") && embedded.size() == image.size()) {
          image = embedded.convertToFormat(QImage::Format_ARGB32);
          result.image = image;
        }
      }
      document.imagePath = path;
      document.canvasWidth = image.width();
      document.canvasHeight = image.height();
      QBuffer png(&document.sourceImagePng);
      png.open(QIODevice::WriteOnly);
      image.save(&png, "PNG");
      result.sidecarPath = *sidecar.readPath;
      result.document = document;
      return result;
    }
    result.warnings << QStringLiteral("The annotation file %1 could not be read (%2); opened the image without it.")
                           .arg(QFileInfo(*sidecar.readPath).fileName(), parsed.error.value_or(diagnostic::documentInvalid));
  }

  AnnotationDocument document;
  document.imagePath = path;
  document.imageHash = hash;
  document.canvasWidth = image.width();
  document.canvasHeight = image.height();
  document.createdAt = document.modifiedAt = now();
  QBuffer png(&document.sourceImagePng);
  png.open(QIODevice::WriteOnly);
  image.save(&png, "PNG");
  result.document = document;
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
