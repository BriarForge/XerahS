#include "AppTheme.h"

#include <QApplication>
#include <QWidget>

namespace xerahs::app {

bool isHighContrastSystemPalette(const QPalette &palette) {
  const QColor win = palette.color(QPalette::Window);
  const QColor text = palette.color(QPalette::WindowText);
  const QColor base = palette.color(QPalette::Base);
  const QColor hi = palette.color(QPalette::Highlight);
  const bool blackOnWhite = win == Qt::white && text == Qt::black && base == Qt::white;
  const bool whiteOnBlack = win == Qt::black && text == Qt::white && base == Qt::black;
  return whiteOnBlack || (blackOnWhite && hi == Qt::yellow);
}

bool resolveDarkTheme(ShellThemeMode mode, const QPalette &basePalette) {
  if (mode == ShellThemeMode::Dark) return true;
  if (mode == ShellThemeMode::Light) return false;
  const QColor window = basePalette.color(QPalette::Window);
  const QColor text = basePalette.color(QPalette::WindowText);
  // Follow the OS palette polarity; if Qt is using a neutral light default
  // without a dark platform theme, respect that polarity honestly.
  return window.lightness() < text.lightness();
}

AppThemePalette appThemePalette(bool dark) {
  AppThemePalette p;
  p.dark = dark;
  p.accent = QColor(0x3E, 0x83, 0xF2);
  p.accentEnd = QColor(0x39, 0x75, 0xD5);
  p.accentForeground = QColor(0xFF, 0xFF, 0xFF);

  if (dark) {
    p.surfaceWindow = QColor(0x17, 0x1B, 0x21);
    p.surfacePanel = QColor(0x1C, 0x21, 0x28);
    p.surfaceCard = QColor(0x23, 0x2A, 0x33);
    p.surfaceCardAlt = QColor(0x29, 0x30, 0x39);
    p.surfaceInput = QColor(0x20, 0x26, 0x2F);
    p.surfaceInputHover = QColor(0x26, 0x2D, 0x37);
    p.strokeSubtle = QColor(0x31, 0x39, 0x47);
    p.strokeControl = QColor(0x46, 0x50, 0x61);
    p.textPrimary = QColor(0xF3, 0xF6, 0xF8);
    p.textSecondary = QColor(0xCA, 0xD2, 0xDB);
    p.textTertiary = QColor(0x98, 0xA5, 0xB4);

    p.statusSuccessFill = QColor(0x16, 0x27, 0x1C);
    p.statusSuccessBorder = QColor(0x4C, 0x8E, 0x67);
    p.statusSuccessText = QColor(0xD9, 0xFB, 0xE8);
    p.statusWarningFill = QColor(0x35, 0x26, 0x0D);
    p.statusWarningBorder = QColor(0xD7, 0xA5, 0x50);
    p.statusWarningText = QColor(0xFF, 0xE9, 0xB2);
    p.statusErrorFill = QColor(0x34, 0x16, 0x1B);
    p.statusErrorBorder = QColor(0xDC, 0x5A, 0x68);
    p.statusErrorText = QColor(0xFF, 0xD7, 0xDC);

    p.editorMain = QColor(0x27, 0x27, 0x27);
    p.editorPanel = QColor(0x24, 0x24, 0x24);
    p.editorToolbar = QColor(0x22, 0x22, 0x22);
    p.editorBorder = QColor(0x1F, 0x1F, 0x1F);
    p.editorSeparator = QColor(0x33, 0x33, 0x33);
    p.editorControlBg = QColor(0x2E, 0x2E, 0x2E);
    p.editorControlHover = QColor(0x3A, 0x3A, 0x3A);
    p.editorText = QColor(0xD8, 0xDA, 0xDB);
    p.editorCheckerA = QColor(0x27, 0x27, 0x27);
    p.editorCheckerB = QColor(0x2E, 0x2E, 0x2E);
  } else {
    p.surfaceWindow = QColor(0xF7, 0xF8, 0xFB);
    p.surfacePanel = QColor(0xF1, 0xF3, 0xF7);
    p.surfaceCard = QColor(0xFF, 0xFF, 0xFF);
    p.surfaceCardAlt = QColor(0xF8, 0xFA, 0xFD);
    p.surfaceInput = QColor(0xFC, 0xFD, 0xFF);
    p.surfaceInputHover = QColor(0xF6, 0xF9, 0xFD);
    p.strokeSubtle = QColor(0xD8, 0xDE, 0xE8);
    p.strokeControl = QColor(0xC4, 0xCE, 0xDA);
    p.textPrimary = QColor(0x16, 0x1B, 0x22);
    p.textSecondary = QColor(0x58, 0x64, 0x75);
    p.textTertiary = QColor(0x75, 0x82, 0x93);

    p.statusSuccessFill = QColor(0xEA, 0xF7, 0xEE);
    p.statusSuccessBorder = QColor(0x72, 0xA8, 0x6A);
    p.statusSuccessText = QColor(0x18, 0x36, 0x24);
    p.statusWarningFill = QColor(0xFF, 0xF4, 0xCC);
    p.statusWarningBorder = QColor(0xD7, 0xA5, 0x50);
    p.statusWarningText = QColor(0x6A, 0x4C, 0x00);
    p.statusErrorFill = QColor(0xFD, 0xEC, 0xEC);
    p.statusErrorBorder = QColor(0xDC, 0x35, 0x45);
    p.statusErrorText = QColor(0x7A, 0x16, 0x22);

    p.editorMain = QColor(0xF5, 0xF5, 0xF7);
    p.editorPanel = QColor(0xFD, 0xFD, 0xFD);
    p.editorToolbar = QColor(0xFD, 0xFD, 0xFD);
    p.editorBorder = QColor(0xE5, 0xE5, 0xE8);
    p.editorSeparator = QColor(0xE3, 0xE3, 0xE3);
    p.editorControlBg = QColor(0xF5, 0xF5, 0xF7);
    p.editorControlHover = QColor(0xEB, 0xEB, 0xEF);
    p.editorText = QColor(0x4E, 0x4E, 0x4E);
    p.editorCheckerA = QColor(0xFF, 0xFF, 0xFF);
    p.editorCheckerB = QColor(0xEB, 0xEB, 0xEB);
  }
  return p;
}

QPalette buildThemeQPalette(const AppThemePalette &t) {
  QPalette pal;
  for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    const bool dis = (group == QPalette::Disabled);
    pal.setColor(group, QPalette::Window, t.surfacePanel);
    pal.setColor(group, QPalette::WindowText, dis ? t.textTertiary : t.textPrimary);
    pal.setColor(group, QPalette::Base, t.surfaceInput);
    pal.setColor(group, QPalette::AlternateBase, t.surfaceCardAlt);
    pal.setColor(group, QPalette::Text, dis ? t.textTertiary : t.textPrimary);
    pal.setColor(group, QPalette::Button, t.surfaceCard);
    pal.setColor(group, QPalette::ButtonText, dis ? t.textTertiary : t.textPrimary);
    pal.setColor(group, QPalette::Highlight, t.accent);
    pal.setColor(group, QPalette::HighlightedText, t.accentForeground);
    pal.setColor(group, QPalette::Link, t.accent);
    pal.setColor(group, QPalette::PlaceholderText, t.textTertiary);
    pal.setColor(group, QPalette::ToolTipBase, t.surfaceCard);
    pal.setColor(group, QPalette::ToolTipText, t.textPrimary);
    pal.setColor(group, QPalette::Mid, t.strokeSubtle);
  }
  return pal;
}

QString shellThemeStyleSheet(const AppThemePalette &t) {
  const QString accentSubtle = t.dark ? QStringLiteral("rgba(62, 131, 242, 0.16)") : QStringLiteral("rgba(62, 131, 242, 0.12)");
  const QString accentHover = t.dark ? QStringLiteral("rgba(62, 131, 242, 0.24)") : QStringLiteral("rgba(62, 131, 242, 0.18)");
  const QString accentBorder = QStringLiteral("rgba(62, 131, 242, 0.45)");
  return QStringLiteral(R"(
QMainWindow#applicationWindow {
  background-color: %1;
  color: %9;
}
QMenuBar {
  background-color: %1;
  color: %9;
  border-bottom: 1px solid %7;
  padding: 2px 6px;
}
QMenuBar::item {
  background: transparent;
  padding: 5px 10px;
  border-radius: 4px;
}
QMenuBar::item:selected {
  background-color: %6;
}
QMenu {
  background-color: %3;
  color: %9;
  border: 1px solid %7;
  border-radius: 6px;
  padding: 4px;
}
QMenu::item {
  padding: 6px 24px 6px 12px;
  border-radius: 4px;
}
QMenu::item:selected {
  background-color: %12;
  color: %13;
}
QMenu::separator {
  height: 1px;
  background-color: %7;
  margin: 4px 8px;
}

QFrame#navigationPane {
  background-color: %2;
  border-right: 1px solid %7;
}
QLineEdit#navigationSearch, QLineEdit {
  background-color: %5;
  color: %9;
  border: 1px solid %8;
  border-radius: 6px;
  padding: 5px 10px;
  min-height: 22px;
}
QLineEdit#navigationSearch:focus, QLineEdit:focus {
  border: 1px solid %12;
  background-color: %6;
}

QPushButton[navRole="page"], QPushButton[navRole="group"], QToolButton[navRole="action"] {
  background-color: transparent;
  color: %9;
  border: 1px solid transparent;
  border-radius: 8px;
  padding: 7px 10px;
  text-align: left;
  font-weight: 500;
}
QToolButton[navRole="action"] {
  padding: 6px 10px 6px 28px;
  font-weight: 400;
  color: %10;
}
QPushButton[navRole="page"][navChild="true"] {
  padding: 6px 10px 6px 28px;
  font-weight: 400;
}
QPushButton[navRole="page"]:hover, QPushButton[navRole="group"]:hover, QToolButton[navRole="action"]:hover {
  background-color: %6;
  color: %9;
}
QPushButton[navRole="page"][navActive="true"] {
  background-color: %14;
  border: 1px solid %16;
  color: %9;
  font-weight: 600;
}
QPushButton[navRole="page"]:disabled, QToolButton[navRole="action"]:disabled {
  color: %11;
}

QWidget#contentFrameHost {
  background-color: %2;
}
QFrame[card="true"] {
  background-color: %3;
  border: 1px solid %7;
  border-radius: 8px;
}
QFrame[headerBar="true"] {
  background-color: %1;
  border-bottom: 1px solid %7;
}
QFrame[footerBar="true"] {
  background-color: %1;
  border-top: 1px solid %7;
}
QFrame#shellStatusStrip {
  background-color: %1;
  border-top: 1px solid %7;
}

QLabel[role="pageTitle"] {
  font-size: 22px;
  font-weight: 600;
  color: %9;
}
QLabel[role="pageTitleAccent"] {
  font-size: 22px;
  font-weight: 600;
  color: %12;
}
QLabel[role="sectionTitle"] {
  font-size: 14px;
  font-weight: 600;
  color: %9;
}
QLabel[role="caption"] {
  font-size: 12px;
  color: %10;
}
QLabel[role="statValue"] {
  font-size: 26px;
  font-weight: 600;
  color: %9;
}

QPushButton {
  background-color: %14;
  color: %9;
  border: 1px solid %16;
  border-radius: 6px;
  padding: 6px 14px;
}
QPushButton:hover {
  background-color: %15;
  border-color: %12;
}
QPushButton:disabled {
  color: %11;
  background-color: %4;
  border-color: %7;
}
QPushButton[accent="true"] {
  background-color: %12;
  color: %13;
  border: 1px solid %12;
  font-weight: 600;
}
QPushButton[accent="true"]:hover {
  background-color: %17;
}
QPushButton[settingsRow="true"] {
  background-color: transparent;
  border: 1px solid transparent;
  border-radius: 8px;
  padding: 10px 12px;
  text-align: left;
}
QPushButton[settingsRow="true"]:hover {
  background-color: %4;
}

QComboBox, QSpinBox {
  background-color: %5;
  color: %9;
  border: 1px solid %8;
  border-radius: 6px;
  padding: 5px 10px;
}
QTabWidget::pane {
  border: none;
  background-color: %2;
}
QTabBar::tab {
  background-color: transparent;
  color: %10;
  padding: 8px 16px;
  border-bottom: 2px solid transparent;
}
QTabBar::tab:selected {
  color: %9;
  border-bottom: 2px solid %12;
  font-weight: 600;
}
QTabBar::tab:hover {
  color: %9;
}
QListWidget, QPlainTextEdit {
  background-color: %3;
  color: %9;
  border: 1px solid %7;
  border-radius: 6px;
}
QListWidget::item:selected {
  background-color: %14;
  color: %9;
  border-radius: 4px;
}
QScrollArea {
  background: transparent;
  border: none;
}
QProgressBar {
  background-color: %4;
  border: 1px solid %7;
  border-radius: 4px;
  text-align: center;
  max-height: 12px;
}
QProgressBar::chunk {
  background-color: %12;
  border-radius: 3px;
}
)")
      .arg(t.surfaceWindow.name(), t.surfacePanel.name(), t.surfaceCard.name(), t.surfaceCardAlt.name(),
           t.surfaceInput.name(), t.surfaceInputHover.name(), t.strokeSubtle.name(), t.strokeControl.name(),
           t.textPrimary.name())
      .arg(t.textSecondary.name(), t.textTertiary.name(), t.accent.name(), t.accentForeground.name(),
           accentSubtle, accentHover, accentBorder, t.accentEnd.name());
}

QString editorThemeStyleSheet(const AppThemePalette &t) {
  return QStringLiteral(R"(
QWidget#editorSurfaceHost, QMainWindow#editorWindow {
  background-color: %1;
  color: %8;
}
QToolBar#editorToolbar, QFrame#editorTopToolbar, QFrame#editorOptionsBar, QFrame#editorBottomBar {
  background-color: %3;
  border: 1px solid %4;
  border-radius: 6px;
  spacing: 3px;
  padding: 3px;
}
QToolBar#editorToolbar::separator {
  background-color: %5;
  width: 1px;
  margin: 4px 4px;
}
QToolBar#editorToolbar QToolButton, QFrame#editorTopToolbar QToolButton, QFrame#editorOptionsBar QToolButton,
QFrame#editorBottomBar QToolButton, QFrame#editorBottomBar QPushButton {
  background-color: %6;
  color: %8;
  border: 1px solid %4;
  border-radius: 4px;
  padding: 5px 7px;
  min-width: 26px;
  min-height: 26px;
}
QToolBar#editorToolbar QToolButton:hover, QFrame#editorTopToolbar QToolButton:hover,
QFrame#editorOptionsBar QToolButton:hover, QFrame#editorBottomBar QToolButton:hover,
QFrame#editorBottomBar QPushButton:hover {
  background-color: %7;
}
QToolBar#editorToolbar QToolButton:checked, QFrame#editorTopToolbar QToolButton:checked,
QFrame#editorOptionsBar QToolButton:checked {
  background-color: %9;
  color: %10;
  border-color: %9;
}
QToolBar#editorToolbar QToolButton:disabled, QFrame#editorBottomBar QToolButton:disabled {
  color: #7A7A7A;
}
)")
      .arg(t.editorMain.name(), t.editorPanel.name(), t.editorToolbar.name(), t.editorBorder.name(),
           t.editorSeparator.name(), t.editorControlBg.name(), t.editorControlHover.name(), t.editorText.name(),
           t.accent.name())
      .arg(t.accentForeground.name());
}

bool applyShellTheme(QWidget &window, ShellThemeMode mode) {
  const QPalette sys = QApplication::palette();
  if (isHighContrastSystemPalette(sys)) {
    window.setStyleSheet({});
    return false;
  }
  const bool dark = resolveDarkTheme(mode, sys);
  const auto tokens = appThemePalette(dark);
  window.setPalette(buildThemeQPalette(tokens));
  window.setStyleSheet(shellThemeStyleSheet(tokens));
  return dark;
}

bool applyEditorTheme(QWidget &window, ShellThemeMode mode) {
  const QPalette sys = QApplication::palette();
  if (isHighContrastSystemPalette(sys)) {
    window.setStyleSheet({});
    return false;
  }
  const bool dark = resolveDarkTheme(mode, sys);
  const auto tokens = appThemePalette(dark);
  window.setStyleSheet(editorThemeStyleSheet(tokens));
  return dark;
}

}  // namespace xerahs::app
