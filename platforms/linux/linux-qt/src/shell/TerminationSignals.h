// Qt Core-only POSIX termination-signal bridge (IFCAT-008). SIGINT and SIGTERM
// become a Qt signal on the event loop instead of killing the process mid-work,
// so the application can cancel cooperatively and exit with a documented status.
#pragma once
#include <QObject>
#include <memory>

namespace xerahs::shell {
class TerminationSignals final : public QObject {
  Q_OBJECT
public:
  // Shell convention (also what an unhandled signal reports): 128 + signal.
  // SIGINT 130, SIGTERM 143.
  static int exitStatus(int signal) { return 128 + signal; }
  // Only one instance may exist; a second construction leaves the first in
  // charge and reports installed() == false.
  explicit TerminationSignals(QObject *parent = nullptr);
  ~TerminationSignals() override;
  bool installed() const;
signals:
  void requested(int signal);
private:
  struct State;
  std::unique_ptr<State> m_state;
};
} // namespace xerahs::shell
