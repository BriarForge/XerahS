// CORE-013, SETCAT-007: credential references live outside destination JSON.
// A libsecret-backed store is a later slice; this is the seam it must implement.
#pragma once

#include <QHash>
#include <QString>
#include <QUuid>

#include <optional>

namespace xerahs::uploads {

enum class CredentialStatus { Ok, NotFound, Unavailable, Denied };

struct CredentialResult {
  CredentialStatus status = CredentialStatus::Unavailable;
  QString value;
  bool ok() const { return status == CredentialStatus::Ok; }
};

class CredentialStore {
public:
  virtual ~CredentialStore() = default;
  virtual CredentialResult store(const QUuid &destinationId, const QString &name, const QString &value) = 0;
  virtual CredentialResult lookup(const QUuid &destinationId, const QString &name) const = 0;
  virtual CredentialResult remove(const QUuid &destinationId, const QString &name) = 0;
  virtual CredentialResult removeAll(const QUuid &destinationId) = 0;
};

// In-process store for tests. setForcedStatus makes every call return that
// status and leaves stored values unchanged.
class MemoryCredentialStore final : public CredentialStore {
public:
  CredentialResult store(const QUuid &destinationId, const QString &name, const QString &value) override;
  CredentialResult lookup(const QUuid &destinationId, const QString &name) const override;
  CredentialResult remove(const QUuid &destinationId, const QString &name) override;
  CredentialResult removeAll(const QUuid &destinationId) override;
  void setForcedStatus(std::optional<CredentialStatus> status);

private:
  CredentialResult gate(const QUuid &destinationId, const QString &name, bool named) const;
  QString key(const QUuid &destinationId) const;

  std::optional<CredentialStatus> m_forced;
  QHash<QString, QHash<QString, QString>> m_secrets;
};

}  // namespace xerahs::uploads
