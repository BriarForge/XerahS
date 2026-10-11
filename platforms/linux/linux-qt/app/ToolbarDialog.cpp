#include "NativeEditorToolbar.h"
#include "NativeEditorAppearance.h"
#include "NativeEditorPreferences.h"
#include "image-editor/ToolbarPreferences.h"
#include <QAccessible>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>
namespace xerahs::app {
using namespace xerahs::editor;
bool manageEditorToolbar(QWidget *parent, const ToolbarActionRegistry &actions) {
  const auto original = nativeEditorPreferences().values.value(u"ToolbarItems").toArray();
  const auto revision = nativeEditorPreferenceRevision();
  QDialog dialog(parent); dialog.setWindowTitle(QStringLiteral("Toolbar")); dialog.setObjectName(QStringLiteral("toolbarDialog"));
  NativeEditorAppearance appearance(dialog);
  auto *layout = new QVBoxLayout(&dialog);
  auto *description = new QLabel(QStringLiteral("Choose commands, groups and additional hotkeys, then Apply. Protected shortcuts and menu commands stay available when hidden. A partly checked group box uses its default. Unavailable commands are preserved with inactive hotkeys."), &dialog);
  description->setWordWrap(true); layout->addWidget(description);
  auto *table = new QTableWidget(0, 4, &dialog); table->setObjectName(QStringLiteral("toolbarItems"));
  table->setAccessibleName(QStringLiteral("Toolbar command order and settings"));
  table->setHorizontalHeaderLabels({QStringLiteral("Command"), QStringLiteral("Show"), QStringLiteral("Start group"), QStringLiteral("Additional hotkey")});
  table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table->setColumnWidth(1, 60); table->setColumnWidth(2, 90); table->setColumnWidth(3, 170); layout->addWidget(table);
  const auto setDraft = [&](QJsonArray items) {
    QSet<QString> present;
    for (const auto &value : items) present.insert(value.toObject().value(u"Id").toString());
    for (const auto &action : toolbarActions()) if (!present.contains(action.id) && items.size() < maximumToolbarItems)
      items.append(QJsonObject{{"Id", action.id}, {"IsVisible", false}, {"BeginGroup", QJsonValue::Null}, {"Hotkey", ""}});
    table->setRowCount(items.size());
    for (int row = 0; row < items.size(); ++row) {
      const auto raw = items.at(row).toObject(); const auto id = raw.value(u"Id").toString();
      auto *action = actions.value(id); const bool editable = action && knownToolbarAction(id);
      auto *command = new QTableWidgetItem(action ? action->text().remove(QLatin1Char('&')) : QStringLiteral("Unavailable: %1").arg(id));
      command->setData(Qt::UserRole, raw); command->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled); table->setItem(row, 0, command);
      for (int column : {1, 2}) {
        auto *check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | (editable ? Qt::ItemIsUserCheckable : Qt::NoItemFlags));
        if (column == 2 && editable) check->setFlags(check->flags() | Qt::ItemIsUserTristate);
        const auto value = raw.value(column == 1 ? u"IsVisible" : u"BeginGroup");
        check->setCheckState(!editable ? Qt::Unchecked : column == 1 ? (value.toBool(true) ? Qt::Checked : Qt::Unchecked) :
            value.isBool() ? (value.toBool() ? Qt::Checked : Qt::Unchecked) : Qt::PartiallyChecked);
        table->setItem(row, column, check);
      }
      auto *key = new QTableWidgetItem(editable ? raw.value(u"Hotkey").toString() : QStringLiteral("Preserved"));
      key->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | (editable ? Qt::ItemIsEditable : Qt::NoItemFlags));
      table->setItem(row, 3, key);
    }
    if (table->rowCount()) table->setCurrentCell(0, 0);
  };
  setDraft(original);
  auto *moves = new QHBoxLayout; layout->addLayout(moves);
  for (const auto &entry : {std::pair{QStringLiteral("Move up"), -1}, {QStringLiteral("Move down"), 1}}) {
    auto *button = new QPushButton(entry.first, &dialog);
    button->setObjectName(entry.second < 0 ? QStringLiteral("toolbarUp") : QStringLiteral("toolbarDown")); moves->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, &dialog, [&, step = entry.second] {
      const int from = table->currentRow(), to = from + step;
      if (from < 0 || to < 0 || to >= table->rowCount()) return;
      for (int column = 0; column < table->columnCount(); ++column) {
        auto *a = table->takeItem(from, column); auto *b = table->takeItem(to, column);
        table->setItem(from, column, b); table->setItem(to, column, a);
      }
      table->setCurrentCell(to, 0);
    });
  }
  moves->addStretch();
  auto *error = new QLabel(&dialog); error->setObjectName(QStringLiteral("toolbarError")); error->setWordWrap(true); layout->addWidget(error);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Apply")); buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("applyToolbar"));
  buttons->button(QDialogButtonBox::RestoreDefaults)->setObjectName(QStringLiteral("resetToolbar"));
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true); layout->addWidget(buttons);
  QObject::connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] { setDraft(resetToolbarItems(original)); error->clear(); });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    table->setFocus();
    QJsonArray draft;
    for (int row = 0; row < table->rowCount(); ++row) {
      auto raw = table->item(row, 0)->data(Qt::UserRole).value<QJsonObject>(); const auto id = raw.value(u"Id").toString();
      if (knownToolbarAction(id) && actions.contains(id)) {
        raw.insert(QStringLiteral("IsVisible"), table->item(row, 1)->checkState() == Qt::Checked);
        const auto group = table->item(row, 2)->checkState();
        raw.insert(QStringLiteral("BeginGroup"), group == Qt::PartiallyChecked ? QJsonValue(QJsonValue::Null) : QJsonValue(group == Qt::Checked));
        raw.insert(QStringLiteral("Hotkey"), table->item(row, 3)->text());
      }
      draft.append(raw);
    }
    const auto written = applyToolbarPreferences(draft, revision);
    if (written.saved) { dialog.accept(); return; }
    error->setText(editorPreferenceDiagnosticMessage(written.diagnostic)); table->setFocus();
    QAccessibleEvent alert(error, QAccessible::Alert); QAccessible::updateAccessibility(&alert);
  });
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  dialog.resize(680, 440);
  return dialog.exec() == QDialog::Accepted;
}
} // namespace xerahs::app
