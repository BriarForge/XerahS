// CORE-007: native command palette over the commands already in CommandRegistry.
// One shared QAction per stable id. The palette never enables a command and
// never treats closing the dialog as proof that the command succeeded.
#pragma once

#include "CommandRegistry.h"

#include <QDialog>
#include <QPointer>

class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QShowEvent;

namespace xerahs::app {

class CommandPalette final : public QDialog {
  Q_OBJECT
public:
  // Qt::UserRole values on each commandPaletteCommands row.
  static constexpr int CommandIdRole = Qt::UserRole;
  static constexpr int CommandLabelRole = Qt::UserRole + 1;
  static constexpr int CommandShortcutRole = Qt::UserRole + 2;

  explicit CommandPalette(CommandRegistry &registry, QWidget *parent = nullptr);
  ~CommandPalette() override;

public slots:
  void accept() override;
  void reject() override;

protected:
  void showEvent(QShowEvent *event) override;
  void changeEvent(QEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  struct CommandRow {
    QString id;
    QString registeredLabel;
    QPointer<QAction> action;
    QListWidgetItem *item = nullptr;
  };

  void reload();
  void clearRows();
  void connectRow(CommandRow &row);
  void refreshRow(const QString &id);
  void dropRow(const QString &id);
  void applyFilter();
  void moveSelection(int delta);
  void explainSelection();
  void setStatusText(const QString &message, bool alert);
  bool finishDialog(int result);
  void cancelDispatch();
  void dispatch(quint64 ticket);
  QString labelFor(const CommandRow &row) const;
  QString unavailableMessage(const QAction &action, const QString &label) const;
  CommandRow *rowFor(const QString &id);
  const CommandRow *rowFor(const QString &id) const;
  CommandRow *selectedRow();
  bool runnable(const QAction *action) const;
  bool modalityReleased() const;

  QPointer<CommandRegistry> m_registry;
  QPointer<QWidget> m_owner;
  bool m_hadOwner = false;
  QLineEdit *m_search = nullptr;
  QListWidget *m_commands = nullptr;
  QPushButton *m_run = nullptr;
  QLabel *m_status = nullptr;
  QList<CommandRow> m_rows;
  QString m_pendingId;
  QPointer<QAction> m_pendingAction;
  quint64 m_ticket = 0;
  bool m_armed = false;
  bool m_reloading = false;
  bool m_allowDone = false;
};

}  // namespace xerahs::app
