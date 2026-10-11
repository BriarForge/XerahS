#include "EditorAppearance.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace xerahs::editor {
namespace {
constexpr Argb black = 0xFF000000, white = 0xFFFFFFFF;
double luminance(Argb color) {
  const auto channel = [](unsigned value) {
    const double s = value / 255.0;
    return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel((color >> 16) & 255) + 0.7152 * channel((color >> 8) & 255) + 0.0722 * channel(color & 255);
}
Argb composite(Argb foreground, Argb background) {
  const unsigned alpha = foreground >> 24;
  Argb result = 0xFF000000;
  for (int shift : {0, 8, 16}) {
    const unsigned a = (foreground >> shift) & 255, b = (background >> shift) & 255;
    result |= ((a * alpha + b * (255 - alpha) + 127) / 255) << shift;
  }
  return result;
}
Argb bestText(Argb background) {
  return appearanceContrastRatio(black, background) >= appearanceContrastRatio(white, background) ? black : white;
}
Argb readable(Argb text, Argb background, double minimum) {
  const auto opaque = composite(text, background);
  return appearanceContrastRatio(opaque, background) >= minimum ? opaque : bestText(background);
}
AppearancePalette manualPalette(bool dark) {
  AppearancePalette result;
  if (dark) {
    result.window = result.button = 0xFF292929; result.base = 0xFF171717; result.alternateBase = 0xFF232323;
    result.windowText = result.text = result.buttonText = 0xFFF5F5F5;
    result.link = 0xFF8AB4F8; result.placeholderText = 0xFFB0B0B0;
  }
  return result;
}
}
DesktopAppearance decodeDesktopAppearance(const QJsonObject &settings) {
  DesktopAppearance result;
  const auto scheme = settings.value(u"color-scheme");
  if (scheme == QJsonValue(1)) result.colorScheme = DesktopColorScheme::Dark;
  else if (scheme == QJsonValue(2)) result.colorScheme = DesktopColorScheme::Light;
  result.highContrast = settings.value(u"contrast") == QJsonValue(1);
  result.reducedMotion = settings.value(u"reduced-motion") == QJsonValue(1);
  const auto accent = settings.value(u"accent-color");
  if (accent.isArray() && accent.toArray().size() == 3) {
    const auto components = accent.toArray(); Argb rgb = 0xFF000000; bool valid = true;
    for (int i = 0; i < 3; ++i) {
      const auto value = components.at(i); const double component = value.toDouble(-1);
      if (!value.isDouble() || !std::isfinite(component) || component < 0 || component > 1) { valid = false; break; }
      rgb |= Argb(std::lround(component * 255)) << (16 - i * 8);
    }
    if (valid) result.accent = rgb;
  }
  return result;
}
double appearanceContrastRatio(Argb a, Argb b) {
  const double x = luminance(a), y = luminance(b);
  return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}
ResolvedEditorAppearance resolveEditorAppearance(const EditorPreferences &preferences,
    const DesktopAppearance &desktop, const AppearancePalette &native) {
  ResolvedEditorAppearance result; result.highContrast = desktop.highContrast; result.reducedMotion = desktop.reducedMotion;
  const auto &values = preferences.values;
  const QString theme = values.value(u"Theme").toString();
  const bool system = values.value(u"UseSystemTheme").toBool(true) || theme == u"System";
  const bool nativeDark = luminance(native.window) < luminance(native.windowText);
  const bool dark = system ? (desktop.colorScheme == DesktopColorScheme::Unspecified ? nativeDark : desktop.colorScheme == DesktopColorScheme::Dark) : theme != u"Light";
  auto &p = result.palette;
  p = system && dark == nativeDark ? native : manualPalette(dark);
  if (desktop.highContrast) {
    p.window = p.base = p.alternateBase = p.button = dark ? black : white;
    p.windowText = p.text = p.buttonText = p.link = p.placeholderText = dark ? white : black;
  }
  // Native palette colors can contain alpha. Produce opaque display roles while
  // retaining the user's original canonical accent in the preference record.
  p.window = composite(p.window, dark ? black : white); p.base = composite(p.base, p.window);
  p.button = composite(p.button, p.window); p.alternateBase = composite(p.alternateBase, p.base);
  const double minimum = desktop.highContrast ? 7.0 : 4.5;
  p.windowText = readable(p.windowText, p.window, minimum); p.text = readable(p.text, p.base, minimum);
  p.buttonText = readable(p.buttonText, p.button, minimum); p.placeholderText = readable(p.placeholderText, p.base, minimum);
  // Text also appears on alternating item rows. Fall back to the base color if
  // the same foreground cannot keep those rows legible.
  if (appearanceContrastRatio(p.text, p.alternateBase) < minimum) p.alternateBase = p.base;
  const Argb saved = parseColor(values.value(u"AccentColorHex").toString()).value_or(0xFF3E83F2);
  const Argb requested = values.value(u"UseSystemAccentColor").toBool(true) ? desktop.accent.value_or(native.highlight) : saved;
  p.highlight = composite(requested, p.base);
  const auto usableAccent = [&](Argb color) {
    return appearanceContrastRatio(color, p.base) >= 3.0 && appearanceContrastRatio(color, p.window) >= 3.0 &&
        appearanceContrastRatio(color, p.button) >= 3.0 && appearanceContrastRatio(bestText(color), color) >= minimum;
  };
  if (!usableAccent(p.highlight)) {
    // Prefer a neutral with the greatest contrast across the actual surfaces.
    // If a native palette has incompatible light/dark surfaces, normalize those
    // surfaces together rather than losing a visible selection/focus color.
    Argb replacement = bestText(p.base); double best = -1;
    for (unsigned n = 0; n <= 255; ++n) {
      const Argb candidate = 0xFF000000 | (n << 16) | (n << 8) | n;
      if (!usableAccent(candidate)) continue;
      const double score = std::min({appearanceContrastRatio(candidate, p.base), appearanceContrastRatio(candidate, p.window), appearanceContrastRatio(candidate, p.button)});
      if (score > best) { best = score; replacement = candidate; }
    }
    if (best < 0) {
      p.window = p.button = p.base; p.windowText = p.buttonText = p.text;
    }
    p.highlight = replacement; result.accentAdjustedForContrast = true;
  }
  p.highlightedText = readable(p.highlightedText, p.highlight, minimum);
  p.link = readable(p.highlight, p.base, minimum);
  return result;
}
}  // namespace xerahs::editor
