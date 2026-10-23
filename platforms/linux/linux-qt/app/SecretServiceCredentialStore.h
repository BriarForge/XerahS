// CORE-013, SETCAT-007: upload secrets live in the desktop Secret Service
// (GNOME Keyring, KWallet, KeePassXC), never in destination JSON.
// Calls block on D-Bus and may wait for an unlock prompt; run them off the GUI thread.
#pragma once

#include "uploads/CredentialStore.h"

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QMetaType>

namespace xerahs::app {

// org.freedesktop.Secret (oayays).
struct SecretValue {
  QDBusObjectPath session;
  QByteArray parameters;
  QByteArray value;
  QString contentType;
};
QDBusArgument &operator<<(QDBusArgument &argument, const SecretValue &secret);
const QDBusArgument &operator>>(const QDBusArgument &argument, SecretValue &secret);

inline const QString kSecretSchema = QStringLiteral("com.xerahs.app.UploadCredential");

class SecretServiceCredentialStore final : public uploads::CredentialStore {
public:
  explicit SecretServiceCredentialStore(QDBusConnection bus = QDBusConnection::sessionBus(),
                                        QString service = QStringLiteral("org.freedesktop.secrets"), int promptTimeoutMs = 120000);
  uploads::CredentialResult store(const QUuid &destinationId, const QString &name, const QString &value) override;
  uploads::CredentialResult lookup(const QUuid &destinationId, const QString &name) const override;
  uploads::CredentialResult remove(const QUuid &destinationId, const QString &name) override;
  uploads::CredentialResult removeAll(const QUuid &destinationId) override;

private:
  using Status = uploads::CredentialStatus;
  Status openSession() const;
  Status search(const QMap<QString, QString> &attributes, QList<QDBusObjectPath> &items) const;
  Status unlock(const QList<QDBusObjectPath> &objects) const;
  Status prompt(const QDBusObjectPath &path, QDBusVariant *result = nullptr) const;
  Status deleteItems(const QList<QDBusObjectPath> &items) const;

  mutable QDBusConnection m_bus;
  QString m_service;
  int m_promptTimeoutMs;
  mutable QDBusObjectPath m_session;
};

}  // namespace xerahs::app

Q_DECLARE_METATYPE(xerahs::app::SecretValue)
