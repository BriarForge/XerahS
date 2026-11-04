#include "NativeAccessibility.h"
// CORE-024, CORE-025.
#include "WatchFoldersDialog.h"
#include "WatchFolderService.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHash>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace xerahs::app {
namespace {
// User-facing sentence for a watch-folder diagnostic, followed by the stable code (as the video converter does).
// This helper sits outside the class, so it names the translation context itself; the code stays untranslated.
QString watchMessage(const QString &code) {
  static const QHash<QString, QString> sentences{
    {"watch-directory-required", QCoreApplication::translate("WatchFoldersDialog", "Choose a folder to watch.")},
    {"watch-directory-denied", QCoreApplication::translate("WatchFoldersDialog", "XerahS cannot read that folder.")},
    {"watch-system-root", QCoreApplication::translate("WatchFoldersDialog", "System folders cannot be watched.")},
    {"watch-capture-loop", QCoreApplication::translate("WatchFoldersDialog", "That folder contains the capture folder, which would upload captures in a loop.")},
    {"watch-overlapping-roots", QCoreApplication::translate("WatchFoldersDialog", "That folder overlaps a folder that is already watched.")},
    {"watch-filter-required", QCoreApplication::translate("WatchFoldersDialog", "Enter at least one file pattern, for example *.png.")},
    {"watch-filter-invalid", QCoreApplication::translate("WatchFoldersDialog", "One of the file patterns is not valid.")},
    {"watch-filter-duplicate", QCoreApplication::translate("WatchFoldersDialog", "A file pattern is listed twice.")},
    {"watch-filter-limit", QCoreApplication::translate("WatchFoldersDialog", "Too many file patterns for one folder.")},
    {"watch-store-full", QCoreApplication::translate("WatchFoldersDialog", "The maximum number of watch folders has been reached.")},
    {"watch-action-invalid", QCoreApplication::translate("WatchFoldersDialog", "Choose what to do with new files.")},
    {"watch-rule-invalid", QCoreApplication::translate("WatchFoldersDialog", "This watch folder setting is not valid.")},
    {"watch-duplicate-id", QCoreApplication::translate("WatchFoldersDialog", "This watch folder is already listed.")},
    {"watch-store-permissions", QCoreApplication::translate("WatchFoldersDialog", "Watch folder settings are not private to your user. Check their permissions.")},
    {"watch-store-unsafe", QCoreApplication::translate("WatchFoldersDialog", "The watch folder settings location is unsafe.")},
    {"watch-store-write-failed", QCoreApplication::translate("WatchFoldersDialog", "Watch folder settings could not be saved.")},
  };
  if (code.isEmpty()) return {};
  const auto sentence = sentences.value(code);
  return sentence.isEmpty() ? code : QCoreApplication::translate("WatchFoldersDialog", "%1 (%2)").arg(sentence, code);
}
} // namespace
WatchFoldersDialog::WatchFoldersDialog(WatchFolderService &service, QWidget *parent) : QDialog(parent) {
  new NativeAccessibility(*this, tr("Manage watched folders and actions. Missing folders remain visible. Close ends editing; saved rules remain available."), true);
  setWindowTitle(tr("Watch folders")); resize(650, 460);
  auto *layout = new QVBoxLayout(this);
  auto *description = new QLabel(tr("New matching files are processed after size and modification time stay unchanged for 2 seconds. Upload uses the default destination. Hidden, temporary and capture-output files are ignored. Failed files are reported and retried on the next launch."), this);
  description->setWordWrap(true); layout->addWidget(description);
  auto *list = new QListWidget(this); list->setObjectName("watchFolders"); list->setAccessibleName(tr("Watched folders")); layout->addWidget(list);
  auto *form = new QFormLayout;
  auto *pathRow = new QHBoxLayout;
  auto *path = new QLineEdit(this); path->setObjectName("watchPath");
  auto *browse = new QPushButton(tr("Browse…"), this); pathRow->addWidget(path); pathRow->addWidget(browse); form->addRow(tr("Folder:"), pathRow); path->setAccessibleName(tr("Watch folder path"));
  // The default globs are file patterns, not prose: they stay as they are.
  auto *filters = new QLineEdit("*.png *.jpg *.jpeg *.gif *.webp", this); filters->setObjectName("watchFilters"); form->addRow(tr("File &globs (space separated):"), filters);
  auto *recursive = new QCheckBox(tr("Include subfolders"), this); recursive->setObjectName("watchRecursive"); form->addRow(recursive);
  auto *action = new QComboBox(this); action->setObjectName("watchAction"); action->addItems({tr("Upload to default destination"), tr("Copy into captures folder"), tr("Move into captures folder (removes source)")}); form->addRow(tr("&Action:"), action);
  layout->addLayout(form);
  auto *error = new QLabel(this); error->setObjectName("watchError"); error->setWordWrap(true); layout->addWidget(error);
  auto *buttons = new QHBoxLayout;
  auto *add = new QPushButton(tr("Add folder"), this); add->setObjectName("addWatchFolder");
  auto *remove = new QPushButton(tr("Remove selected"), this); remove->setObjectName("removeWatchFolder");
  remove->setEnabled(false);
  buttons->addWidget(add); buttons->addWidget(remove); layout->addLayout(buttons);
  auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this); layout->addWidget(close); connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
  // This helper does not capture the dialog, so it names the translation context itself.
  const auto refresh = [list, remove, &service] {
    const QSignalBlocker block(list); list->clear();
    for (const auto &rule : service.rules()) {
      auto *item = new QListWidgetItem(rule.path + (QFileInfo(rule.path).isDir() ? "" : QCoreApplication::translate("WatchFoldersDialog", " (folder missing; restore it or remove this rule)")) + " — " + rule.filters.join(' ') + (rule.recursive ? QCoreApplication::translate("WatchFoldersDialog", " (subfolders)") : "") +
          (rule.action == watch::Action::Upload ? QCoreApplication::translate("WatchFoldersDialog", " — Upload") : rule.action == watch::Action::Copy ? QCoreApplication::translate("WatchFoldersDialog", " — Copy") : QCoreApplication::translate("WatchFoldersDialog", " — Move")), list);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    }
    remove->setEnabled(list->currentRow() >= 0);
  };
  refresh();
  connect(browse, &QPushButton::clicked, this, [this, path] { const auto selected = QFileDialog::getExistingDirectory(this, tr("Watch folder")); if (!selected.isEmpty()) path->setText(selected); });
  connect(add, &QPushButton::clicked, this, [=, &service] {
    auto rules = service.rules(); watch::Rule rule; rule.path = path->text(); rule.filters = filters->text().split(QRegularExpression("[;\\s]+"), Qt::SkipEmptyParts);
    rule.recursive = recursive->isChecked(); rule.action = watch::Action(action->currentIndex()); rules.append(rule);
    const auto diagnostic = service.setRules(rules); error->setText(watchMessage(diagnostic));
    if (diagnostic.isEmpty()) refresh();
  });
  connect(remove, &QPushButton::clicked, this, [=, &service] {
    const auto row = list->currentRow(); auto rules = service.rules();
    if (row < 0 || row >= rules.size()) { error->setText(tr("Select a watch folder to remove.")); return; }
    rules.removeAt(row); const auto diagnostic = service.setRules(rules); error->setText(watchMessage(diagnostic)); if (diagnostic.isEmpty()) refresh();
  });
  connect(list, &QListWidget::itemChanged, this, [=, &service](QListWidgetItem *item) {
    auto rules = service.rules(); const auto row = list->row(item); if (row < 0 || row >= rules.size()) return;
    rules[row].enabled = item->checkState() == Qt::Checked; error->setText(watchMessage(service.setRules(rules))); refresh();
  });
  connect(list, &QListWidget::currentRowChanged, this, [=](int row) { remove->setEnabled(row >= 0); });
}
}
