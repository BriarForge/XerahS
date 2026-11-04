// Native controls keep their intrinsic names, keyboard conventions and palettes.
// Root purpose and changing status text remain available to assistive clients.
#pragma once
#include "NativeWindowPlacement.h"
#include <QAccessible>
#include <QLabel>
namespace xerahs::app {
class NativeAccessibility final : public QObject {
public:
  NativeAccessibility(QWidget &window, const QString &purpose, bool managePlacement = true) : QObject(&window), m_window(window) {
    setObjectName("nativeAccessibility");
    if (window.accessibleDescription().isEmpty()) window.setAccessibleDescription(purpose);
    if (managePlacement) new NativeWindowPlacement(window);
    qApp->installEventFilter(this); refresh();
  }
  void refresh() {
    const QPointer<NativeAccessibility> owner(this);
    QList<QPointer<QLabel>> labels;
    for (auto *label : m_window.findChildren<QLabel *>()) labels.append(label);
    for (const auto &current : labels) {
      if (!current) continue;
      auto *label = current.data();
      if (label->wordWrap() && label->width() > 0) {
        const auto key = "_xerahsOriginalMinimumHeight";
        if (!label->property(key).isValid()) label->setProperty(key, label->minimumHeight());
        if (!owner) return;
        if (!current) continue;
        const int height = std::max(label->property(key).toInt(), label->heightForWidth(label->width()));
        if (label->minimumHeight() != height) label->setMinimumHeight(height);
        if (!owner) return;
        if (!current) continue;
      }
      const auto identity = (label->objectName() + ' ' + label->accessibleName()).toLower();
      if (!identity.contains("status") && !identity.contains("error") && !identity.contains("validation") && !identity.contains("diagnostic")) continue;
      if (label->accessibleDescription() == label->text()) continue;
      label->setAccessibleDescription(label->text());
      if (!owner) return;
      if (!current) continue;
      QAccessibleEvent update(label, identity.contains("error") && !label->text().isEmpty() ? QAccessible::Alert : QAccessible::DescriptionChanged);
      QAccessible::updateAccessibility(&update);
      if (!owner) return;
    }
  }

protected:
  bool eventFilter(QObject *object, QEvent *event) override {
    auto *widget = qobject_cast<QWidget *>(object);
    if (!widget || (widget != &m_window && !m_window.isAncestorOf(widget))) return false;
    if ((event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show || event->type() == QEvent::Paint) && !m_queued) {
      m_queued = true;
      QTimer::singleShot(0, this, [this] { m_queued = false; refresh(); });
    }
    return false;
  }
private:
  QWidget &m_window;
  bool m_queued = false;
};
}
