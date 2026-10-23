// CORE-024, CORE-025.
#include "WatchFoldersDialog.h"
#include "WatchFolderService.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace xerahs::app {
WatchFoldersDialog::WatchFoldersDialog(WatchFolderService &service, QWidget *parent) : QDialog(parent) {
  setWindowTitle("Watch folders"); resize(650, 460);
  auto *layout = new QVBoxLayout(this);
  auto *description = new QLabel("New matching files are processed after size and modification time stay unchanged for 2 seconds. Upload uses the default destination. Hidden, temporary and capture-output files are ignored. Failed files are reported and retried on the next launch.", this);
  description->setWordWrap(true); layout->addWidget(description);
  auto *list = new QListWidget(this); list->setObjectName("watchFolders"); list->setAccessibleName("Watched folders"); layout->addWidget(list);
  auto *form = new QFormLayout;
  auto *pathRow = new QHBoxLayout;
  auto *path = new QLineEdit(this); path->setObjectName("watchPath");
  auto *browse = new QPushButton("Browse…", this); pathRow->addWidget(path); pathRow->addWidget(browse); form->addRow("Folder:", pathRow); path->setAccessibleName("Watch folder path");
  auto *filters = new QLineEdit("*.png *.jpg *.jpeg *.gif *.webp", this); filters->setObjectName("watchFilters"); form->addRow("File &globs (space separated):", filters);
  auto *recursive = new QCheckBox("Include subfolders", this); recursive->setObjectName("watchRecursive"); form->addRow(recursive);
  auto *action = new QComboBox(this); action->setObjectName("watchAction"); action->addItems({"Upload to default destination", "Copy into captures folder", "Move into captures folder (removes source)"}); form->addRow("&Action:", action);
  layout->addLayout(form);
  auto *error = new QLabel(this); error->setObjectName("watchError"); error->setWordWrap(true); layout->addWidget(error);
  auto *buttons = new QHBoxLayout;
  auto *add = new QPushButton("Add folder", this); add->setObjectName("addWatchFolder");
  auto *remove = new QPushButton("Remove selected", this); remove->setObjectName("removeWatchFolder");
  buttons->addWidget(add); buttons->addWidget(remove); layout->addLayout(buttons);
  auto *close = new QDialogButtonBox(QDialogButtonBox::Close, this); layout->addWidget(close); connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
  const auto refresh = [list, &service] {
    const QSignalBlocker block(list); list->clear();
    for (const auto &rule : service.rules()) {
      auto *item = new QListWidgetItem(rule.path + " — " + rule.filters.join(' ') + (rule.recursive ? " (subfolders)" : "") +
          (rule.action == watch::Action::Upload ? " — Upload" : rule.action == watch::Action::Copy ? " — Copy" : " — Move"), list);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    }
  };
  refresh();
  connect(browse, &QPushButton::clicked, this, [this, path] { const auto selected = QFileDialog::getExistingDirectory(this, "Watch folder"); if (!selected.isEmpty()) path->setText(selected); });
  connect(add, &QPushButton::clicked, this, [=, &service] {
    auto rules = service.rules(); watch::Rule rule; rule.path = path->text(); rule.filters = filters->text().split(QRegularExpression("[;\\s]+"), Qt::SkipEmptyParts);
    rule.recursive = recursive->isChecked(); rule.action = watch::Action(action->currentIndex()); rules.append(rule);
    const auto diagnostic = service.setRules(rules); error->setText(diagnostic);
    if (diagnostic.isEmpty()) refresh();
  });
  connect(remove, &QPushButton::clicked, this, [=, &service] {
    const auto row = list->currentRow(); auto rules = service.rules(); if (row < 0 || row >= rules.size()) return;
    rules.removeAt(row); const auto diagnostic = service.setRules(rules); error->setText(diagnostic); if (diagnostic.isEmpty()) refresh();
  });
  connect(list, &QListWidget::itemChanged, this, [=, &service](QListWidgetItem *item) {
    auto rules = service.rules(); const auto row = list->row(item); if (row < 0 || row >= rules.size()) return;
    rules[row].enabled = item->checkState() == Qt::Checked; error->setText(service.setRules(rules)); refresh();
  });
}
}
