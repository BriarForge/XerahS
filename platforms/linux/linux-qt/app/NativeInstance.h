// Native Linux lifetime lock, authenticated local socket and bounded intake.
#pragma once
#include "shell/ActivationProtocol.h"
#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <functional>

class QLocalSocket;
namespace xerahs::app {
struct NativeInstanceOptions {
  int timeoutMs = 2000;
  int maximumClients = 16;
  // Test/native I/O seam; production obtains kernel SO_PEERCRED on both ends.
  std::function<std::optional<quint64>(qintptr)> peerUser;
};
struct InstanceStart {
  enum class Kind { Primary, Forwarded, Busy, Failed } kind = Kind::Failed;
  QString diagnostic;
};
class NativeInstance final : public QObject {
  Q_OBJECT
public:
  using Admission = std::function<xerahs::shell::ActivationReplyState(const xerahs::shell::ActivationRequest &)>;
  explicit NativeInstance(QString directory, NativeInstanceOptions options = {}, QObject *parent = nullptr);
  ~NativeInstance() override;
  static QString runtimeDirectory();
  InstanceStart start(const xerahs::shell::ActivationRequest &request);
  void setAdmission(Admission admission) { m_admission = std::move(admission); }
  bool primary() const { return m_server.isListening() && m_lockFd >= 0; }
  QString endpoint() const;
signals:
  void activationRequested(const xerahs::shell::ActivationRequest &request);
private:
  struct Receipt { QByteArray fingerprint; xerahs::shell::ActivationReply reply; };
  InstanceStart forward(const xerahs::shell::ActivationRequest &request);
  bool openDirectory();
  bool authenticate(qintptr descriptor) const;
  void acceptConnections();
  void consume(QLocalSocket *socket);
  void reject(QLocalSocket *socket, const QString &diagnostic, const QString &id = {});
  void sendReply(QLocalSocket *socket, const xerahs::shell::ActivationReply &reply);
  QString m_directory;
  NativeInstanceOptions m_options;
  QLocalServer m_server;
  int m_directoryFd = -1, m_lockFd = -1;
  bool m_started = false;
  Admission m_admission;
  QSet<QLocalSocket *> m_clients;
  QHash<QString, Receipt> m_receipts;
  QQueue<QString> m_receiptOrder;
};
} // namespace xerahs::app
Q_DECLARE_METATYPE(xerahs::shell::ActivationRequest)
