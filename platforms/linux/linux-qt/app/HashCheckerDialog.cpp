#include "HashCheckerDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace xerahs::app {

using tools::HashAlgorithm;

namespace {

QStringList localFilePaths(const QMimeData *data) {
  QStringList paths;
  if (!data) return paths;
  for (const auto &url : data->urls())
    if (url.isLocalFile()) paths.append(url.toLocalFile());
  return paths;
}

void acceptLocalFiles(QDropEvent *event) {
  if (localFilePaths(event->mimeData()).isEmpty()) event->ignore();
  else event->acceptProposedAction();
}

// EU-006: shown whenever a weak algorithm is chosen and with its results.
QString weakWarning() {
  return QStringLiteral("CRC32, MD5 and SHA-1 are not suitable for security verification; use SHA-256 or stronger to check authenticity.");
}

int percentOf(qint64 hashed, qint64 total) {
  if (total <= 0) return 0;
  return static_cast<int>(std::min<qint64>(100, hashed * 100 / total));
}

QString verdictFor(const QString &digest, const QString &expected) {
  if (digest.isEmpty() || expected.trimmed().isEmpty()) return {};
  return tools::digestMatches(digest, expected) ? QStringLiteral("Match") : QStringLiteral("No match");
}

}  // namespace

HashCheckerDialog::HashCheckerDialog(QWidget *parent) : QDialog(parent) {
  setObjectName(QStringLiteral("hashCheckerDialog"));
  setWindowTitle(QStringLiteral("Hash checker"));
  setAccessibleName(QStringLiteral("Hash checker"));
  setAcceptDrops(true);

  m_file = new QLineEdit(this);
  m_file->setObjectName(QStringLiteral("hashFile"));
  m_file->setReadOnly(true);
  m_file->setAccessibleName(QStringLiteral("File"));
  m_choose = new QPushButton(QStringLiteral("Choose &file…"), this);
  m_choose->setObjectName(QStringLiteral("chooseHashFile"));
  m_algorithm = new QComboBox(this);
  m_algorithm->setObjectName(QStringLiteral("hashAlgorithm"));
  m_algorithm->setAccessibleName(QStringLiteral("Algorithm"));
  for (const auto &name : tools::hashAlgorithmNames()) m_algorithm->addItem(name);
  m_algorithm->setCurrentIndex(int(HashAlgorithm::Sha256));  // EU-005: SHA-256 is the default.
  m_compute = new QPushButton(QStringLiteral("&Compute"), this);
  m_compute->setObjectName(QStringLiteral("computeHash"));
  m_cancel = new QPushButton(QStringLiteral("&Cancel hashing"), this);
  m_cancel->setObjectName(QStringLiteral("cancelHash"));
  m_digest = new QLineEdit(this);
  m_digest->setObjectName(QStringLiteral("hashDigest"));
  m_digest->setReadOnly(true);
  m_digest->setAccessibleName(QStringLiteral("Digest"));
  m_copy = new QPushButton(QStringLiteral("&Copy"), this);
  m_copy->setObjectName(QStringLiteral("copyHash"));
  m_compare = new QLineEdit(this);
  m_compare->setObjectName(QStringLiteral("hashCompare"));
  m_compare->setAccessibleName(QStringLiteral("Compare with"));
  m_compare->setPlaceholderText(QStringLiteral("Paste an expected hash"));
  m_verdict = new QLabel(this);
  m_verdict->setObjectName(QStringLiteral("hashVerdict"));
  m_verdict->setAccessibleName(QStringLiteral("Comparison result"));
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("hashStatus"));
  m_status->setTextFormat(Qt::PlainText);
  m_status->setWordWrap(true);
  m_progress = new QProgressBar(this);
  m_progress->setObjectName(QStringLiteral("hashProgress"));
  m_progress->setRange(0, 100);
  m_progress->setAccessibleName(QStringLiteral("Hash progress"));
  m_weak = new QLabel(weakWarning(), this);
  m_weak->setObjectName(QStringLiteral("hashWeakWarning"));
  m_weak->setWordWrap(true);
  m_weak->setVisible(false);
  m_buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  m_buttons->setObjectName(QStringLiteral("hashButtons"));

  auto *fileRow = new QHBoxLayout;
  fileRow->addWidget(m_file, 1);
  fileRow->addWidget(m_choose);
  auto *form = new QFormLayout;
  form->addRow(QStringLiteral("File:"), fileRow);
  form->addRow(QStringLiteral("Algorithm:"), m_algorithm);
  form->addRow(QString(), m_weak);
  form->addRow(QStringLiteral("Compare with:"), m_compare);
  auto *digestRow = new QHBoxLayout;
  digestRow->addWidget(m_digest, 1);
  digestRow->addWidget(m_copy);
  form->addRow(QStringLiteral("Hash:"), digestRow);
  auto *run = new QHBoxLayout;
  run->addWidget(m_compute);
  run->addWidget(m_cancel);
  run->addWidget(m_verdict);
  run->addStretch();
  run->addWidget(m_buttons);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addLayout(run);
  layout->addWidget(m_progress);
  layout->addWidget(m_status);
  resize(620, 240);

  m_status->setText(QStringLiteral("Choose or drop a file, then compute its hash."));
  m_cancel->setEnabled(false);
  m_copy->setEnabled(false);

  connect(m_choose, &QPushButton::clicked, this, &HashCheckerDialog::chooseFile);
  connect(m_compute, &QPushButton::clicked, this, &HashCheckerDialog::compute);
  connect(m_cancel, &QPushButton::clicked, this, &HashCheckerDialog::cancel);
  connect(m_copy, &QPushButton::clicked, this, &HashCheckerDialog::copyDigest);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_compare, &QLineEdit::textChanged, this, [this] { updateComparison(); });
  connect(m_algorithm, &QComboBox::currentIndexChanged, this, [this](int index) {
    m_weak->setVisible(tools::hashAlgorithmIsWeak(static_cast<HashAlgorithm>(index)));
  });
  // Escape and a close must answer a running read instead of leaving it orphaned.
  connect(this, &QDialog::finished, this, &HashCheckerDialog::cancel);
}

QString HashCheckerDialog::file() const { return m_file->text(); }

QString HashCheckerDialog::digest() const { return m_digest->text(); }

QString HashCheckerDialog::comparison() const { return m_verdict->text(); }

QString HashCheckerDialog::statusText() const { return m_status->text(); }

bool HashCheckerDialog::busy() const { return m_running; }

void HashCheckerDialog::setFile(const QString &path) {
  if (busy() || path.isEmpty()) return;
  m_file->setText(path);
  compute();
}

void HashCheckerDialog::chooseFile() {
  if (busy()) return;
  const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose a file to hash"));
  if (!path.isEmpty()) setFile(path);
}

void HashCheckerDialog::compute() {
  if (busy()) return;
  const auto path = m_file->text();
  if (path.isEmpty()) {
    m_status->setText(QStringLiteral("Choose a file before computing a hash."));
    return;
  }
  runHashWork(path, static_cast<HashAlgorithm>(m_algorithm->currentIndex()));
}

void HashCheckerDialog::cancel() {
  if (!m_cancelled) return;
  m_work.cancel();
  m_cancelled->store(true);
  m_progress->setEnabled(false);
  m_status->setText(QStringLiteral("Cancelling…"));
}

void HashCheckerDialog::copyDigest() {
  if (m_digest->text().isEmpty()) return;
  QApplication::clipboard()->setText(m_digest->text());
  m_status->setText(QStringLiteral("Hash copied to the clipboard."));
}

void HashCheckerDialog::setBusy(bool busy) {
  if (m_running == busy) return;
  m_running = busy;
  m_choose->setEnabled(!busy);
  m_algorithm->setEnabled(!busy);
  m_compute->setEnabled(!busy);
  m_cancel->setEnabled(busy);
  emit busyChanged(busy);
}

void HashCheckerDialog::runHashWork(const QString &path, HashAlgorithm algorithm) {
  m_cancelled = std::make_shared<std::atomic_bool>(false);
  m_digest->clear();
  m_copy->setEnabled(false);
  m_verdict->clear();
  m_progress->setValue(0);
  m_progress->setEnabled(true);
  m_status->setText(QStringLiteral("Hashing %1 with %2…")
                        .arg(QFileInfo(path).fileName(), tools::hashAlgorithmName(algorithm)));
  setBusy(true);

  const auto cancelled = m_cancelled;
  m_work = OwnedWork::run<tools::HashResult>(this,
      [path, algorithm, cancelled](const OwnedWork::Context &context) {
        return tools::hashFile(path, algorithm, cancelled.get(),
            [&context, cancelled](qint64 done, qint64 total) {
              if (context.cancelled()) cancelled->store(true);
              context.progress(done, total);
            });
      }, [this, path, algorithm](tools::HashResult result) {
        setBusy(false);
        showResult(result, path, algorithm);
        emit hashed(result.ok ? result.hex : QString());
      }, [this](qint64 done, qint64 total) {
        if (!busy()) return;
        if (total <= 0) {
          m_progress->setRange(0, 0);
          return;
        }
        m_progress->setRange(0, 100);
        m_progress->setValue(percentOf(done, total));
      });
}

void HashCheckerDialog::showResult(const tools::HashResult &result, const QString &path,
                                   HashAlgorithm algorithm) {
  if (!result.ok) {
    m_progress->setValue(0);
    m_status->setText(result.error == QStringLiteral("cancelled")
                          ? QStringLiteral("Hashing was cancelled.")
                          : QStringLiteral("The file could not be read, so no hash was computed."));
    return;
  }
  m_progress->setValue(100);
  m_digest->setText(result.hex);
  m_digest->setToolTip(QStringLiteral("%1 of %2")
                           .arg(tools::hashAlgorithmName(algorithm), QFileInfo(path).fileName()));
  m_copy->setEnabled(true);
  updateComparison();
  m_status->setText(QStringLiteral("%1 hashed with %2.%3")
                        .arg(QFileInfo(path).fileName(), tools::hashAlgorithmName(algorithm),
                             tools::hashAlgorithmIsWeak(algorithm) ? QStringLiteral(" ") + weakWarning() : QString()));
}

void HashCheckerDialog::updateComparison() {
  m_verdict->setText(verdictFor(m_digest->text(), m_compare->text()));
}

void HashCheckerDialog::dragEnterEvent(QDragEnterEvent *event) { acceptLocalFiles(event); }

void HashCheckerDialog::dragMoveEvent(QDragMoveEvent *event) { acceptLocalFiles(event); }

void HashCheckerDialog::dropEvent(QDropEvent *event) {
  const auto paths = localFilePaths(event->mimeData());
  if (paths.isEmpty()) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
  setFile(paths.first());
}

}  // namespace xerahs::app