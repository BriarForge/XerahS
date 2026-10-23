#include "CredentialStore.h"

namespace xerahs::uploads {

CredentialResult MemoryCredentialStore::gate(const QUuid &destinationId, const QString &name, bool named) const {
  if (m_forced) return {*m_forced, {}};
  if (destinationId.isNull() || (named && name.isEmpty())) return {CredentialStatus::Denied, {}};
  return {CredentialStatus::Ok, {}};
}

QString MemoryCredentialStore::key(const QUuid &destinationId) const {
  return destinationId.toString(QUuid::WithoutBraces);
}

CredentialResult MemoryCredentialStore::store(const QUuid &destinationId, const QString &name, const QString &value) {
  const auto gated = gate(destinationId, name, true);
  if (!gated.ok()) return gated;
  m_secrets[key(destinationId)][name] = value;
  return {CredentialStatus::Ok, {}};
}

CredentialResult MemoryCredentialStore::lookup(const QUuid &destinationId, const QString &name) const {
  const auto gated = gate(destinationId, name, true);
  if (!gated.ok()) return gated;
  const auto destination = m_secrets.constFind(key(destinationId));
  if (destination == m_secrets.cend()) return {CredentialStatus::NotFound, {}};
  const auto secret = destination->constFind(name);
  if (secret == destination->cend()) return {CredentialStatus::NotFound, {}};
  return {CredentialStatus::Ok, *secret};
}

CredentialResult MemoryCredentialStore::remove(const QUuid &destinationId, const QString &name) {
  const auto gated = gate(destinationId, name, true);
  if (!gated.ok()) return gated;
  auto destination = m_secrets.find(key(destinationId));
  if (destination == m_secrets.end() || !destination->contains(name)) return {CredentialStatus::NotFound, {}};
  destination->remove(name);
  if (destination->isEmpty()) m_secrets.erase(destination);
  return {CredentialStatus::Ok, {}};
}

CredentialResult MemoryCredentialStore::removeAll(const QUuid &destinationId) {
  const auto gated = gate(destinationId, {}, false);
  if (!gated.ok()) return gated;
  m_secrets.remove(key(destinationId));
  return {CredentialStatus::Ok, {}};
}

void MemoryCredentialStore::setForcedStatus(std::optional<CredentialStatus> status) { m_forced = status; }

}  // namespace xerahs::uploads
