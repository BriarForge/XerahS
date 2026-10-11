#include "NativeEditorAppearance.h"
#include "NativeEditorPreferences.h"
#include <QApplication>
#include <QEvent>
#include <QPalette>
#include <QTimer>
#include <QWidget>

namespace xerahs::app {
using namespace xerahs::editor;
namespace {
AppearancePalette translated(const QPalette &p) {
  AppearancePalette result;
  result.window = p.color(QPalette::Window).rgba(); result.windowText = p.color(QPalette::WindowText).rgba();
  result.base = p.color(QPalette::Base).rgba(); result.text = p.color(QPalette::Text).rgba(); result.alternateBase = p.color(QPalette::AlternateBase).rgba();
  result.button = p.color(QPalette::Button).rgba(); result.buttonText = p.color(QPalette::ButtonText).rgba();
  result.highlight = p.color(QPalette::Highlight).rgba(); result.highlightedText = p.color(QPalette::HighlightedText).rgba();
  result.link = p.color(QPalette::Link).rgba(); result.placeholderText = p.color(QPalette::PlaceholderText).rgba();
  return result;
}
}
NativeEditorAppearance::NativeEditorAppearance(QWidget &window, NativeDesktopAppearance &source)
    : QObject(&window), m_window(window), m_source(source) {
  setObjectName(QStringLiteral("editorAppearance"));
  connect(&nativeEditorPreferenceEvents(), &NativeEditorPreferenceEvents::appearanceChanged, this, &NativeEditorAppearance::refresh);
  connect(&source, &NativeDesktopAppearance::changed, this, &NativeEditorAppearance::refresh);
  qApp->installEventFilter(this); refresh();
}
bool NativeEditorAppearance::eventFilter(QObject *, QEvent *event) {
  if (event->type() == QEvent::ApplicationPaletteChange && !m_queued) {
    m_queued = true; QTimer::singleShot(0, this, [this] { m_queued = false; refresh(); });
  }
  return false;
}
void NativeEditorAppearance::refresh() {
  // QApplication stays the unmodified native source. Only editor roots and
  // their native child controls/dialogs receive resolved overrides.
  const auto native = QApplication::palette();
  const auto result = resolveEditorAppearance(nativeEditorPreferences(), m_source.appearance(), translated(native));
  const auto &p = result.palette; QPalette palette = native;
  for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    for (const auto &entry : {std::pair{QPalette::Window, p.window}, {QPalette::WindowText, p.windowText},
        {QPalette::Base, p.base}, {QPalette::Text, p.text}, {QPalette::AlternateBase, p.alternateBase},
        {QPalette::Button, p.button}, {QPalette::ButtonText, p.buttonText}, {QPalette::Highlight, p.highlight},
        {QPalette::HighlightedText, p.highlightedText}, {QPalette::Link, p.link}, {QPalette::LinkVisited, p.link},
        {QPalette::PlaceholderText, p.placeholderText}, {QPalette::ToolTipBase, p.base}, {QPalette::ToolTipText, p.text}})
      palette.setColor(group, entry.first, QColor::fromRgba(entry.second));
  }
  if (m_window.palette() != palette) m_window.setPalette(palette);
}
}  // namespace xerahs::app
