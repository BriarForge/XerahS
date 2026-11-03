#include "TerminationSignals.h"
#include <QSocketNotifier>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>

namespace xerahs::shell {
namespace {
int g_writeFd = -1;
TerminationSignals *g_owner = nullptr;
extern "C" void onTerminationSignal(int signal) {
  // Async-signal-safe: one byte down the self-pipe, errno preserved.
  const int saved = errno;
  const unsigned char byte = static_cast<unsigned char>(signal);
  if (g_writeFd >= 0) { const ssize_t ignored = ::write(g_writeFd, &byte, 1); (void)ignored; }
  errno = saved;
}
}  // namespace

struct TerminationSignals::State {
  int fds[2] = {-1, -1};
  struct sigaction previousInterrupt {}, previousTerminate {};
  std::unique_ptr<QSocketNotifier> notifier;
  bool installed = false;
};

TerminationSignals::TerminationSignals(QObject *parent) : QObject(parent), m_state(std::make_unique<State>()) {
  if (g_owner) return;
  if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, m_state->fds) != 0) return;
  g_owner = this;
  g_writeFd = m_state->fds[1];
  struct sigaction action {};
  action.sa_handler = onTerminationSignal;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  sigaction(SIGINT, &action, &m_state->previousInterrupt);
  sigaction(SIGTERM, &action, &m_state->previousTerminate);
  m_state->notifier = std::make_unique<QSocketNotifier>(m_state->fds[0], QSocketNotifier::Read);
  connect(m_state->notifier.get(), &QSocketNotifier::activated, this, [this] {
    unsigned char byte = 0;
    while (::read(m_state->fds[0], &byte, 1) == 1) emit requested(int(byte));
  });
  m_state->installed = true;
}

TerminationSignals::~TerminationSignals() {
  if (!m_state || !m_state->installed) {
    if (m_state && m_state->fds[0] >= 0) { ::close(m_state->fds[0]); ::close(m_state->fds[1]); }
    return;
  }
  sigaction(SIGINT, &m_state->previousInterrupt, nullptr);
  sigaction(SIGTERM, &m_state->previousTerminate, nullptr);
  g_writeFd = -1;
  g_owner = nullptr;
  m_state->notifier.reset();
  ::close(m_state->fds[0]);
  ::close(m_state->fds[1]);
}

bool TerminationSignals::installed() const { return m_state && m_state->installed; }
} // namespace xerahs::shell
