#include "AnnotationObjectNavigator.h"
#include "EditorCanvas.h"
#include <QAbstractListModel>
#include <QApplication>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QScopeGuard>

namespace xerahs::app {
using namespace xerahs::editor;
class AnnotationObjectModel final : public QAbstractListModel {
public:
  struct Row { QUuid id; QString label, description; };
  explicit AnnotationObjectModel(QObject *parent) : QAbstractListModel(parent) {}
  int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(rows.size()); }
  QVariant data(const QModelIndex &index, int role) const override {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto &row = rows[size_t(index.row())];
    if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole)
      return (selected.contains(row.id) ? QStringLiteral("[selected] ") : QString()) + row.label;
    if (role == Qt::ToolTipRole || role == Qt::AccessibleDescriptionRole) return row.description;
    if (role == Qt::UserRole) return row.id;
    return {};
  }
  Qt::ItemFlags flags(const QModelIndex &index) const override {
    return index.isValid() ? Qt::ItemIsSelectable | Qt::ItemIsEnabled : Qt::NoItemFlags;
  }
  bool replace(const AnnotationDocument &document, quint64 history, quint64 generation) {
    std::vector<Row> prepared;
    prepared.reserve(document.annotations.size());
    const QRegularExpression safeType("[^A-Za-z0-9._:-]");
    for (const auto &annotation : document.annotations) {
      const auto *rectangle = std::get_if<RectangleAnnotation>(&annotation);
      const auto &extra = rectangle ? rectangle->extra : std::get<UnsupportedAnnotation>(annotation).raw;
      const bool hidden = rectangle ? !rectangle->visible : extra.value("visible") == QJsonValue(false);
      const bool unknownVisibility = !rectangle && extra.contains("visible") && !extra.value("visible").isBool();
      QStringList state{unknownVisibility ? "visibility unknown" : hidden ? "hidden" : "visible"};
      if (extra.value("locked") == QJsonValue(true)) state << "locked";
      else if (extra.contains("locked") && !extra.value("locked").isBool()) state << "protection unknown";
      state << (annotationEditable(annotation) ? "unlocked" : "protected");
      QString type = "Rectangle";
      if (!rectangle) {
        // Unknown type names are display hints only. Bound and neutralize
        // markup, control characters and bidi formatting before native text.
        QString name = extra.value("type").toString().left(64);
        name.replace(safeType, "_");
        type = name.isEmpty() ? "Unsupported object" : "Unsupported: " + name;
        state << "geometry editing unavailable";
      }
      const auto id = annotationId(annotation);
      QString description = "Object ID: " + id.toString(QUuid::WithoutBraces) + ". " + state.join(", ") + ".";
      if (rectangle) description += QString(" Source-pixel bounds: left %1, top %2, right %3, bottom %4.")
          .arg(QString::number(rectangle->left(), 'g', 17), QString::number(rectangle->top(), 'g', 17),
               QString::number(rectangle->right(), 'g', 17), QString::number(rectangle->bottom(), 'g', 17));
      prepared.push_back({id, QString::number(prepared.size() + 1) + ". " + type + " — " + state.join(", "), description});
    }
    const QPointer<AnnotationObjectModel> owner(this);
    beginResetModel(); if (!owner) return false;
    rows = std::move(prepared); stateId = history; documentGeneration = generation; initialized = true; endResetModel();
    return bool(owner);
  }
  void selection(const QSet<QUuid> &value) {
    if (selected == value) return;
    selected = value;
    if (!rows.empty()) emit dataChanged(index(0), index(rowCount() - 1), {Qt::DisplayRole, Qt::AccessibleTextRole});
  }
  std::vector<Row> rows;
  QSet<QUuid> selected;
  quint64 stateId = 0, documentGeneration = 0;
  bool initialized = false;
};
AnnotationObjectNavigator::AnnotationObjectNavigator(EditorCanvas &canvas, QWidget *parent, std::function<bool()> ready)
    : QListView(parent), m_canvas(&canvas), m_objects(new AnnotationObjectModel(this)), m_ready(std::move(ready)) {
  setObjectName("annotationObjects"); setAccessibleName("Annotation objects");
  setAccessibleDescription("Objects are listed from bottom to top. Up and Down select; Shift extends a range; Control+Space toggles the focused row. "
      "Escape clears selection. Enter returns to the canvas for move, resize, rotation, order and Delete. Hidden and locked objects are protected; unsupported data stays preserved.");
  setToolTip(accessibleDescription()); setModel(m_objects); setSelectionMode(QAbstractItemView::ExtendedSelection);
  setEditTriggers(QAbstractItemView::NoEditTriggers); setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(0, 0); setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding); setUniformItemSizes(true);
  connect(selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] { publishSelection(); });
  connect(&canvas, &EditorCanvas::changed, this, &AnnotationObjectNavigator::refresh);
  connect(&canvas, &EditorCanvas::toolChanged, this, &AnnotationObjectNavigator::refresh);
  connect(&canvas, &EditorCanvas::viewportChanged, this, &AnnotationObjectNavigator::refresh);
  connect(&canvas, &QObject::destroyed, this, [this] { m_canvas = nullptr; setEnabled(false); });
  refresh();
}
void AnnotationObjectNavigator::refresh() {
  if (m_refreshing || !m_canvas) return;
  const QPointer<AnnotationObjectNavigator> owner(this); const QPointer<QItemSelectionModel> selectionOwner(selectionModel());
  const bool blocked = selectionOwner->blockSignals(true); m_refreshing = true;
  const auto restore = qScopeGuard([owner, selectionOwner, blocked] {
    if (selectionOwner) selectionOwner->blockSignals(blocked); if (owner) owner->m_refreshing = false;
  });
  const auto cursor = currentIndex().data(Qt::UserRole).toUuid();
  if (!m_objects->initialized || m_objects->stateId != m_canvas->annotationState() || m_objects->documentGeneration != m_canvas->documentGeneration())
    if (!m_objects->replace(m_canvas->annotationDocument(), m_canvas->annotationState(), m_canvas->documentGeneration())) return;
  if (!owner || !m_canvas) return;
  m_objects->selection(m_canvas->annotationSelection());
  if (!owner || !m_canvas) return;
  QItemSelection selection; QModelIndex current, firstSelected;
  for (int row = 0; row < m_objects->rowCount(); ++row) {
    const auto index = m_objects->index(row); const auto id = m_objects->rows[size_t(row)].id;
    if (id == cursor) current = index;
    if (m_objects->selected.contains(id)) { selection.select(index, index); if (!firstSelected.isValid()) firstSelected = index; }
  }
  selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
  if (!current.isValid() || (!hasFocus() && !m_objects->selected.isEmpty() && !m_objects->selected.contains(current.data(Qt::UserRole).toUuid()))) current = firstSelected;
  if (!current.isValid() && m_objects->rowCount()) current = m_objects->index(0);
  selectionModel()->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
  setEnabled(m_canvas->isEnabled() && !m_canvas->interactionActive() && (!m_ready || m_ready()));
}
void AnnotationObjectNavigator::publishSelection() {
  if (m_refreshing || !m_canvas) return;
  if (!isEnabled() || m_canvas->interactionActive() || (m_ready && !m_ready())) { refresh(); return; }
  QList<QUuid> ids;
  for (const auto &index : selectionModel()->selectedRows()) ids << index.data(Qt::UserRole).toUuid();
  const QPointer<AnnotationObjectNavigator> owner(this);
  m_canvas->selectAnnotations(ids, m_objects->stateId, m_objects->documentGeneration);
  if (owner) refresh();
}
void AnnotationObjectNavigator::focusObjects() {
  const QPointer<AnnotationObjectNavigator> owner(this);
  refresh(); if (owner && isEnabled()) setFocus(Qt::ShortcutFocusReason);
}
bool AnnotationObjectNavigator::event(QEvent *event) {
  if (event->type() == QEvent::ShortcutOverride) {
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->matches(QKeySequence::SelectAll)) { event->accept(); return true; }
  }
  return QListView::event(event);
}
void AnnotationObjectNavigator::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) { clearSelection(); event->accept(); return; }
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (m_canvas && isEnabled()) m_canvas->setFocus(Qt::ShortcutFocusReason);
    event->accept(); return;
  }
  QListView::keyPressEvent(event);
}
}
