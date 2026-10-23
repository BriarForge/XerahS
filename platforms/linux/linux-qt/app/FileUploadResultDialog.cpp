// CORE-020, CORE-023.
#include "FileUploadResultDialog.h"

#include <QDesktopServices>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QClipboard>
#include <QUrl>

namespace xerahs::app {
namespace {
QLabel *line(QWidget *parent, const QString &name, const QString &text) {
  auto *label = new QLabel(text, parent);
  label->setObjectName(name);
  label->setWordWrap(true);
  label->setTextFormat(Qt::PlainText);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}
QLineEdit *selectable(QWidget *parent, const QString &name, const QString &text) {
  auto *edit = new QLineEdit(text, parent);
  edit->setObjectName(name);
  edit->setReadOnly(true);
  edit->setCursor(Qt::IBeamCursor);
  return edit;
}
}  // namespace

FileUploadResultDialog::FileUploadResultDialog(const QString &fileName, const uploads::UploadOutcome &outcome, QWidget *parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("fileUploadResultDialog"));
  setWindowTitle(QStringLiteral("File uploaded"));
  setModal(false);
  setWindowModality(Qt::NonModal);
  setAttribute(Qt::WA_DeleteOnClose);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(line(this, QStringLiteral("uploadFileName"), fileName));
  m_url = selectable(this, QStringLiteral("uploadUrl"), outcome.url);
  m_url->setAccessibleName(QStringLiteral("Upload URL"));
  layout->addWidget(m_url);
  auto *copy = new QPushButton(QStringLiteral("Copy URL"), this);
  copy->setObjectName(QStringLiteral("copyUploadUrl"));
  auto *open = new QPushButton(QStringLiteral("Open in browser"), this);
  open->setObjectName(QStringLiteral("openUploadUrl"));
  layout->addWidget(copy);
  layout->addWidget(open);
  if (!outcome.deletionUrl.isEmpty()) {
    layout->addWidget(line(this, QStringLiteral("deletionUrlLabel"), QStringLiteral("Deletion URL")));
    auto *deletion = selectable(this, QStringLiteral("deletionUrl"), outcome.deletionUrl);
    deletion->setAccessibleName(QStringLiteral("Deletion URL"));
    layout->addWidget(deletion);
  }
  connect(copy, &QPushButton::clicked, this, &FileUploadResultDialog::copyUrl);
  connect(open, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(m_url->text())); });
  resize(460, sizeHint().height());
}

FileUploadResultDialog::FileUploadResultDialog(const QString &fileName, const QString &message, std::function<void()> openDestinations,
                                               QWidget *parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("fileUploadResultDialog"));
  setWindowTitle(QStringLiteral("File upload"));
  setModal(false);
  setWindowModality(Qt::NonModal);
  setAttribute(Qt::WA_DeleteOnClose);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(line(this, QStringLiteral("uploadFileName"), fileName));
  layout->addWidget(line(this, QStringLiteral("uploadMessage"), message));
  if (openDestinations) {
    auto *button = new QPushButton(QStringLiteral("Upload destinations…"), this);
    button->setObjectName(QStringLiteral("openUploadDestinations"));
    layout->addWidget(button);
    connect(button, &QPushButton::clicked, this, [openDestinations] { openDestinations(); });
  }
  resize(460, sizeHint().height());
}

void FileUploadResultDialog::copyUrl() {
  if (auto *clipboard = QGuiApplication::clipboard()) clipboard->setText(m_url->text());
}
}  // namespace xerahs::app
