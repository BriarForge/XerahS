#include "RecentImageMenu.h"
#include <QCoreApplication>
#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace xerahs::app {
using xerahs::editor::RecentImages;
bool manageRecentImages(QWidget *parent) {
  RecentImages draft = recentImageHistory(); const auto originalRevision = draft.revision();
  QDialog dialog(parent); dialog.setObjectName(QStringLiteral("recentImagesDialog"));
  dialog.setWindowTitle(QCoreApplication::translate("RecentImageMenu", "Recent Images")); dialog.resize(620, 350);
  auto *layout = new QVBoxLayout(&dialog);
  auto *enabled = new QCheckBox(QCoreApplication::translate("RecentImageMenu", "Remember recently opened images"), &dialog);
  enabled->setObjectName(QStringLiteral("rememberRecentImages")); enabled->setChecked(draft.enabled()); layout->addWidget(enabled);
  auto *privacy = new QLabel(nativeEditorPreferencePersistence() ?
      QCoreApplication::translate("RecentImageMenu", "Image paths are saved privately on this computer until you remove them or turn this option off.") :
      QCoreApplication::translate("RecentImageMenu", "Image paths are kept for this session."), &dialog);
  privacy->setObjectName(QStringLiteral("recentImagesPrivacy"));
  privacy->setWordWrap(true); layout->addWidget(privacy);
  auto *form = new QFormLayout; auto *maximum = new QSpinBox(&dialog);
  maximum->setObjectName(QStringLiteral("maximumRecentImages")); maximum->setAccessibleName(QCoreApplication::translate("RecentImageMenu", "Maximum recent images"));
  maximum->setRange(RecentImages::minimumMaximum, RecentImages::maximumMaximum); maximum->setValue(draft.maximum());
  form->addRow(QCoreApplication::translate("RecentImageMenu", "Maximum recent images:"), maximum); layout->addLayout(form);
  auto *paths = new QListWidget(&dialog); paths->setObjectName(QStringLiteral("recentImagePaths"));
  paths->setAccessibleName(QCoreApplication::translate("RecentImageMenu", "Recent image paths")); paths->setSelectionMode(QAbstractItemView::SingleSelection); layout->addWidget(paths);
  auto *row = new QHBoxLayout; auto *remove = new QPushButton(QCoreApplication::translate("RecentImageMenu", "Remove Selected"), &dialog);
  auto *clear = new QPushButton(QCoreApplication::translate("RecentImageMenu", "Clear All"), &dialog);
  remove->setObjectName(QStringLiteral("removeRecentImage")); clear->setObjectName(QStringLiteral("clearRecentImages"));
  row->addWidget(remove); row->addWidget(clear); row->addStretch(); layout->addLayout(row);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QCoreApplication::translate("RecentImageMenu", "Apply"));
  buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("applyRecentImages"));
  buttons->button(QDialogButtonBox::Cancel)->setDefault(true); layout->addWidget(buttons);
  auto refresh = [&] {
    paths->clear(); paths->addItems(draft.paths()); maximum->setEnabled(draft.enabled());
    remove->setEnabled(false); clear->setEnabled(!draft.paths().isEmpty());
  };
  QObject::connect(enabled, &QCheckBox::toggled, &dialog, [&](bool value) { draft.setPreferences(value, maximum->value()); refresh(); });
  QObject::connect(maximum, &QSpinBox::valueChanged, &dialog, [&](int value) { draft.setPreferences(enabled->isChecked(), value); refresh(); });
  QObject::connect(paths, &QListWidget::itemSelectionChanged, &dialog, [&] { remove->setEnabled(paths->currentRow() >= 0); });
  QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
    if (auto *item = paths->currentItem()) draft.remove(item->text());
    refresh();
  });
  QObject::connect(clear, &QPushButton::clicked, &dialog, [&] { draft.clear(); refresh(); });
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  refresh();
  if (dialog.exec() != QDialog::Accepted) return false;
  // An incoming instance request can open a file while this modal loop runs.
  // Do not replace a newer list with the stale customization snapshot.
  if (recentImageHistory().revision() != originalRevision) {
    QMessageBox::information(parent, QCoreApplication::translate("RecentImageMenu", "Recent images changed"),
        QCoreApplication::translate("RecentImageMenu", "Recent images changed while this window was open. Reopen it to apply your changes."));
    return false;
  }
  const auto written = applyRecentImagePreferences(draft, originalRevision);
  if (!written.saved) {
    QMessageBox::warning(parent, QCoreApplication::translate("RecentImageMenu", "Preferences not saved"), editorPreferenceDiagnosticMessage(written.diagnostic));
    return false;
  }
  return true;
}
QMenu *addRecentImagesMenu(QMenu *parent, std::function<void(const QString &)> open, std::function<void()> manage) {
  auto *menu = parent->addMenu(QCoreApplication::translate("RecentImageMenu", "Recent Images")); menu->setObjectName(QStringLiteral("recentImagesMenu"));
  QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, open = std::move(open), manage = std::move(manage)] {
    menu->clear(); const auto &history = recentImageHistory(); int index = 0;
    for (const auto &path : history.paths()) {
      QString label = path; label.replace(QLatin1Char('&'), QStringLiteral("&&"));
      auto *action = menu->addAction(label); action->setObjectName(QStringLiteral("recentImage%1").arg(index++));
      action->setData(path); action->setToolTip(path);
      QObject::connect(action, &QAction::triggered, menu, [open, path] { open(path); });
    }
    if (history.paths().isEmpty()) {
      auto *empty = menu->addAction(history.enabled() ? QCoreApplication::translate("RecentImageMenu", "No recent images") : QCoreApplication::translate("RecentImageMenu", "Recent images are off")); empty->setEnabled(false);
    }
    menu->addSeparator(); auto *preferences = menu->addAction(QCoreApplication::translate("RecentImageMenu", "Manage Recent Images…"));
    preferences->setObjectName(QStringLiteral("manageRecentImages"));
    QObject::connect(preferences, &QAction::triggered, menu, manage);
  });
  return menu;
}
}  // namespace xerahs::app
