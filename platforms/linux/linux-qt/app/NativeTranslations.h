// Live native text bindings. Only declared UI prose is translated; values,
// identifiers and entered text are never discovered or guessed from widgets.
#pragma once
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QVariant>
#include <QWidget>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <functional>
#include <map>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace xerahs::app {
inline QString ownNativeArgument(QStringView value) { return value.toString(); }
inline QString ownNativeArgument(QLatin1StringView value) { return QString(value); }
inline QString ownNativeArgument(const char *value) { return QString::fromUtf8(value); }
inline QString ownNativeArgument(char *value) { return QString::fromUtf8(value); }
template <typename Value> Value ownNativeArgument(Value value) { return value; }
class NativeText {
public:
  NativeText(const char *context, const char *source) : m_context(context), m_source(source) {}
  static NativeText literal(QString value) {
    NativeText result("", ""); result.m_literal = std::move(value); return result;
  }
  QString resolve() const {
    auto text = m_literal ? *m_literal : QCoreApplication::translate(m_context.constData(), m_source.constData());
    for (const auto &substitute : m_arguments) text = substitute(std::move(text));
    return text;
  }
  // Values are captured at the same assignment boundary as ordinary QString
  // formatting. A language change never re-reads a document, draft or store.
  template <typename... Args> NativeText arg(Args... args) const {
    auto next = *this;
    next.m_arguments.emplace_back([values = std::make_tuple(ownNativeArgument(std::move(args))...)](QString text) {
      return std::apply([&](const auto &...value) { return text.arg(value...); }, values);
    });
    return next;
  }
  NativeText append(NativeText suffix) const {
    auto next = *this;
    next.m_arguments.emplace_back([suffix = std::move(suffix)](QString text) { return text + suffix.resolve(); });
    return next;
  }
private:
  QByteArray m_context, m_source;
  std::optional<QString> m_literal;
  std::vector<std::function<QString(QString)>> m_arguments;
};
template <typename Scope> NativeText nativeText(const char *source) {
  return {Scope::staticMetaObject.className(), source};
}
inline NativeText nativeText(const char *context, const char *source) { return {context, source}; }

class NativeTextBindings final : public QObject {
public:
  using Setter = std::function<void(QObject &, const QString &)>;
  using Getter = std::function<QString(QObject &)>;
  explicit NativeTextBindings(QObject &target) : QObject(&target), m_target(&target) {
    // QWidget receives LanguageChange at its window boundary. QAction has no
    // widget event loop, so use the nearest native widget in its ownership tree.
    m_widget = qobject_cast<QWidget *>(&target);
    if (m_widget) m_widget->installEventFilter(this);
    watchWindow();
  }
  void assign(const QByteArray &property, NativeText text) {
    assignValue(property, std::move(text), [property](QObject &target, const QString &value) { target.setProperty(property.constData(), value); },
        [property](QObject &target) { return target.property(property.constData()).toString(); });
  }
  void assignValue(const QByteArray &key, NativeText text, Setter setter, Getter getter = {}) {
    m_entries.insert_or_assign(key, Entry{std::move(text), std::move(setter), std::move(getter), {}, ++m_revision});
    const auto entry = m_entries.at(key);
    const QPointer<NativeTextBindings> alive(this); const QPointer<QObject> target(m_target);
    const auto value = entry.text.resolve();
    if (!alive || !target) return;
    m_entries.at(key).lastText = value;
    entry.set(*target, value);
  }
protected:
  bool eventFilter(QObject *object, QEvent *event) override {
    if (object == m_widget && event->type() == QEvent::ParentChange) { watchWindow(); return false; }
    if (event->type() != QEvent::LanguageChange || object != m_window) return false;
    // Native widgets process their own LanguageChange first (for example a
    // wizard restores cached button text). Apply declared overrides afterward.
    if (!m_queued) {
      m_queued = true;
      QTimer::singleShot(0, this, [this] { m_queued = false; refresh(); });
    }
    return false;
  }
private:
  void refresh() {
    const QPointer<QObject> target(m_target);
    const QPointer<NativeTextBindings> alive(this);
    const auto entries = m_entries;
    for (const auto &[key, entry] : entries) {
      if (!alive || !target) break;
      const auto value = entry.text.resolve();
      if (!alive || !target) break;
      const auto current = m_entries.find(key);
      if (current == m_entries.end() || current->second.revision != entry.revision) continue;
      // A newer ordinary assignment owns the field. A language update must
      // never replace a later failure/status with an earlier bound message.
      if (entry.get) {
        const auto shown = entry.get(*target);
        if (shown != entry.lastText && shown != value) { m_entries.erase(current); continue; }
      }
      current->second.lastText = value;
      entry.set(*target, value);
    }
  }
  struct Entry { NativeText text; Setter set; Getter get; QString lastText; quint64 revision; };
  void watchWindow() {
    if (m_window && m_window != m_widget) m_window->removeEventFilter(this);
    QObject *owner = m_target;
    while (owner && !qobject_cast<QWidget *>(owner)) owner = owner->parent();
    auto *widget = qobject_cast<QWidget *>(owner);
    m_window = widget ? static_cast<QObject *>(widget->window()) : QCoreApplication::instance();
    if (m_window && m_window != m_widget) m_window->installEventFilter(this);
  }
  QPointer<QObject> m_target;
  QPointer<QWidget> m_widget;
  QPointer<QObject> m_window;
  std::map<QByteArray, Entry> m_entries;
  bool m_queued = false;
  quint64 m_revision = 0;
};
inline NativeTextBindings &nativeTextBindings(QObject &target) {
  NativeTextBindings *bindings = nullptr;
  for (auto *child : target.children()) if ((bindings = dynamic_cast<NativeTextBindings *>(child))) break;
  if (!bindings) bindings = new NativeTextBindings(target);
  return *bindings;
}
inline void bindNativeText(QObject &target, const char *property, NativeText text) {
  nativeTextBindings(target).assign(property, std::move(text));
}
template <typename Widget> Widget *translatedWidget(Widget *widget, const char *property, NativeText text) {
  const QPointer<Widget> alive(widget);
  bindNativeText(*widget, property, std::move(text));
  return alive.data();
}
template <typename Buttons, typename Role> QPushButton *addTranslatedButton(Buttons &buttons, NativeText text, Role role) {
  return translatedWidget(buttons.addButton(QString{}, role), "text", std::move(text));
}
inline void addTranslatedRow(QFormLayout &form, NativeText text, QWidget *field) {
  auto *label = translatedWidget(new QLabel(form.parentWidget()), "text", std::move(text));
  label->setBuddy(field);
  form.addRow(label, field);
}
inline void addTranslatedRow(QFormLayout &form, NativeText text, QLayout *field) {
  auto *label = translatedWidget(new QLabel(form.parentWidget()), "text", std::move(text));
  form.addRow(label, field);
}
inline void addTranslatedItem(QComboBox &combo, NativeText text, const QVariant &data = {}) {
  const int row = combo.count(); combo.addItem(QString{}, data);
  const QPersistentModelIndex index(combo.model()->index(row, combo.modelColumn(), combo.rootModelIndex()));
  nativeTextBindings(combo).assignValue("item-" + QByteArray::number(row), std::move(text),
      [index](QObject &target, const QString &value) {
        if (!index.isValid()) return;
        const QSignalBlocker blocker(&target);
        static_cast<QComboBox &>(target).setItemText(index.row(), value);
      });
}
} // namespace xerahs::app
