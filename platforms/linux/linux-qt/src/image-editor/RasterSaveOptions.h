#pragma once
#include "AnnotationDocument.h"
#include <QString>
#include <optional>
namespace xerahs::editor {
enum class RasterFormat { Png, Jpeg, WebP };
struct RasterSaveOptions {
  RasterFormat format = RasterFormat::Png;
  int quality = -1; // No invented default: JPEG/WebP require an explicit 0..100 value.
  std::optional<Argb> jpegMatte;
};
bool validRasterSaveOptions(const RasterSaveOptions &options);
QString rasterFormatId(RasterFormat format);
QString rasterFormatName(RasterFormat format);
QString rasterFormatSuffix(RasterFormat format);
std::optional<RasterFormat> rasterFormatFromSuffix(const QString &suffix);
}
