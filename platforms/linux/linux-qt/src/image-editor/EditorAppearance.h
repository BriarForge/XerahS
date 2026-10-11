// ESET-004/007: appearance policy independent of Qt GUI and desktop I/O.
#pragma once
#include "EditorPreferences.h"

namespace xerahs::editor {
enum class DesktopColorScheme { Unspecified, Dark, Light };
struct DesktopAppearance {
  DesktopColorScheme colorScheme = DesktopColorScheme::Unspecified;
  std::optional<Argb> accent;
  bool highContrast = false;
  bool reducedMotion = false;
};
// Standardized org.freedesktop.appearance keys, translated from native transport.
// Unrecognized or invalid individual values revert only that desktop hint.
DesktopAppearance decodeDesktopAppearance(const QJsonObject &settings);
struct AppearancePalette {
  Argb window = 0xFFF0F0F0, windowText = 0xFF000000;
  Argb base = 0xFFFFFFFF, text = 0xFF000000, alternateBase = 0xFFF5F5F5;
  Argb button = 0xFFF0F0F0, buttonText = 0xFF000000;
  Argb highlight = 0xFF3E83F2, highlightedText = 0xFF000000;
  Argb link = 0xFF0000EE, placeholderText = 0xFF767676;
};
struct ResolvedEditorAppearance {
  AppearancePalette palette;
  bool highContrast = false;
  bool reducedMotion = false;
  bool accentAdjustedForContrast = false;
};
// Colors passed to contrastRatio are opaque display colors. Saved alpha stays
// in preferences; UI colors are composited before contrast resolution.
double appearanceContrastRatio(Argb a, Argb b);
ResolvedEditorAppearance resolveEditorAppearance(const EditorPreferences &preferences,
    const DesktopAppearance &desktop, const AppearancePalette &nativePalette);
}  // namespace xerahs::editor
