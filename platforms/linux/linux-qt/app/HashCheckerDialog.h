// EDITOR-UTILITIES-001 / EU-005, EU-006: the hash checker tool window.
// A file's digest is read on a worker thread, so hashing a large file leaves
// the GUI thread free to show progress and to answer Cancel.
#pragma once

#include "tools/FileHash.h"
#include "OwnedWork.h"

#include <QDialog>

#include <atomic>
#include <memory>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace xerahs::app {

class HashCheckerDialog final : public QDialog {
  Q_OBJECT
public:
  explicit HashCheckerDialog(QWidget *parent = nullptr);

  // Hashes the file at path. A busy dialog keeps its own read and ignores a
  // second file, so an old result never replaces a newer request.
  void setFile(const QString &path);
  QString file() const;
  QString digest() const;
  // "Match" or "No match" once both a digest and an expected value are present.
  QString comparison() const;
  QString statusText() const;
  bool busy() const;

public slots:
  void chooseFile();
  void compute();
  void cancel();
  void copyDigest();

signals:
  void hashed(const QString &hex);
  void busyChanged(bool busy);

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  void setBusy(bool busy);
  void showResult(const tools::HashResult &result, const QString &path, tools::HashAlgorithm algorithm);
  void updateComparison();
  // OwnedWork delivers results and progress on this dialog's thread.
  void runHashWork(const QString &path, tools::HashAlgorithm algorithm);

  QLineEdit *m_file = nullptr;
  QPushButton *m_choose = nullptr;
  QComboBox *m_algorithm = nullptr;
  QPushButton *m_compute = nullptr;
  QPushButton *m_cancel = nullptr;
  QLineEdit *m_digest = nullptr;
  QPushButton *m_copy = nullptr;
  QLineEdit *m_compare = nullptr;
  QLabel *m_verdict = nullptr;
  QLabel *m_weak = nullptr;
  QLabel *m_status = nullptr;
  QProgressBar *m_progress = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
  OwnedWork m_work;

  // FileHash takes an atomic cancellation flag; OwnedWork owns result delivery.
  std::shared_ptr<std::atomic_bool> m_cancelled;
  // Whether a read is in flight. The cancel flag above answers a request to
  // stop; it is not the same question, and cancel() must not end the read early.
  bool m_running = false;
};

}  // namespace xerahs::app