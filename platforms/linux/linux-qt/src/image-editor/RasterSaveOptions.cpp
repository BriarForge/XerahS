#include "RasterSaveOptions.h"
namespace xerahs::editor {
bool validRasterSaveOptions(const RasterSaveOptions &options) {
  switch (options.format) {
    case RasterFormat::Png: return options.quality == -1 && !options.jpegMatte;
    case RasterFormat::Jpeg: return options.quality >= 0 && options.quality <= 100 && options.jpegMatte && (*options.jpegMatte >> 24) == 255;
    case RasterFormat::WebP: return options.quality >= 0 && options.quality <= 100 && !options.jpegMatte;
  }
  return false;
}
QString rasterFormatId(RasterFormat format) {
  switch (format) { case RasterFormat::Png: return "png"; case RasterFormat::Jpeg: return "jpeg"; case RasterFormat::WebP: return "webp"; }
  return {};
}
QString rasterFormatName(RasterFormat format) {
  switch (format) { case RasterFormat::Png: return "PNG"; case RasterFormat::Jpeg: return "JPEG"; case RasterFormat::WebP: return "WebP"; }
  return {};
}
QString rasterFormatSuffix(RasterFormat format) { return format == RasterFormat::Jpeg ? QString("jpg") : rasterFormatId(format); }
std::optional<RasterFormat> rasterFormatFromSuffix(const QString &suffix) {
  const auto value = suffix.toLower();
  if (value == "png") return RasterFormat::Png;
  if (value == "jpg" || value == "jpeg") return RasterFormat::Jpeg;
  if (value == "webp") return RasterFormat::WebP;
  return {};
}
}
