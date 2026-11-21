#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

class QWidget;

namespace xerahs::app {

// Visual tokens matched 1:1 to the ShareX/XerahS reference UI model
// (ThemeResources.axaml and ImageEditorTheme.axaml).
struct AppThemePalette {
  bool dark = true;

  // Shell surfaces (ThemeResources.axaml)
  QColor surfaceWindow;
  QColor surfacePanel;
  QColor surfaceCard;
  QColor surfaceCardAlt;
  QColor surfaceInput;
  QColor surfaceInputHover;
  QColor strokeSubtle;
  QColor strokeControl;
  QColor textPrimary;
  QColor textSecondary;
  QColor textTertiary;
  QColor accent;
  QColor accentEnd;
  QColor accentForeground;

  // Status tokens
  QColor statusSuccessFill;
  QColor statusSuccessBorder;
  QColor statusSuccessText;
  QColor statusWarningFill;
  QColor statusWarningBorder;
  QColor statusWarningText;
  QColor statusErrorFill;
  QColor statusErrorBorder;
  QColor statusErrorText;

  // Editor surfaces (ImageEditorTheme.axaml)
  QColor editorMain;
  QColor editorPanel;
  QColor editorToolbar;
  QColor editorBorder;
  QColor editorSeparator;
  QColor editorControlBg;
  QColor editorControlHover;
  QColor editorText;
  QColor editorCheckerA;
  QColor editorCheckerB;
};

enum class ShellThemeMode { System, Dark, Light };

// True when the active Qt palette is a high-contrast desktop or test palette
// (e.g. pure black/white). Theme stylesheets defer to the native palette so
// high-contrast accessibility is preserved verbatim.
bool isHighContrastSystemPalette(const QPalette &palette);

// Resolve dark/light for the requested mode using the base Qt palette.
bool resolveDarkTheme(ShellThemeMode mode, const QPalette &basePalette);

AppThemePalette appThemePalette(bool dark);
QPalette buildThemeQPalette(const AppThemePalette &tokens);

QString shellThemeStyleSheet(const AppThemePalette &tokens);
QString editorThemeStyleSheet(const AppThemePalette &tokens);

// Apply the ShareX/XerahS shell visual theme to a main window, preserving
// high-contrast system palettes when active.
bool applyShellTheme(QWidget &window, ShellThemeMode mode = ShellThemeMode::System);

// Apply the ShareX.ImageEditor visual theme to an editor host window or widget.
bool applyEditorTheme(QWidget &window, ShellThemeMode mode = ShellThemeMode::System);

}  // namespace xerahs::app
