// CORE-021, SETCAT-014, COMPCAT-011: manage saved upload destinations.
// Credential calls may block on the keyring, so they run on a worker thread.
#pragma once

#include "uploads/CredentialStore.h"
#include "uploads/DestinationCatalog.h"

#include <QDialog>

#include <functional>
#include <optional>

class QLabel;
class QListWidget;
class QPushButton;

namespace xerahs::app {

struct UploadDestinationsPorts {
  std::function<QString(QWidget *)> chooseSxcu;
  std::function<std::optional<QString>(QWidget *, const QString &secretName)> askSecret;
  std::function<bool(QWidget *, const QString &destinationName)> confirmRemove;
  std::function<void(QWidget *, const QString &message)> report;
};
UploadDestinationsPorts nativeUploadDestinationsPorts();

class UploadDestinationsDialog final : public QDialog {
  Q_OBJECT
public:
  UploadDestinationsDialog(uploads::DestinationCatalog &catalog, uploads::CredentialStore &credentials,
                           UploadDestinationsPorts ports = nativeUploadDestinationsPorts(), QWidget *parent = nullptr);
  bool busy() const { return m_busy; }

  void importFile(const QString &path);
  void setSelectedAsDefault();
  void setSecret(const QString &name);
  void removeSelected();

signals:
  void busyChanged(bool busy);
  void secretsChecked();

private:
  void refresh(const QUuid &select = {});
  void showSelected();
  void checkSecrets();
  void setBusy(bool busy);
  void updateButtons();
  QUuid selectedId() const;
  // Runs work on a worker and then done on this dialog's thread, unless the dialog is gone.
  void runCredentialWork(std::function<QVariant()> work, std::function<void(const QVariant &)> done);

  uploads::DestinationCatalog &m_catalog;
  uploads::CredentialStore &m_credentials;
  UploadDestinationsPorts m_ports;
  QListWidget *m_list = nullptr;
  QLabel *m_details = nullptr;
  QListWidget *m_secrets = nullptr;
  QPushButton *m_import = nullptr, *m_default = nullptr, *m_secret = nullptr, *m_remove = nullptr;
  bool m_busy = false;
};

}  // namespace xerahs::app
