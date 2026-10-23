// CORE-020, CORE-023: non-modal outcome of uploading one existing file.
#pragma once

#include "uploads/UploadResponse.h"

#include <QDialog>

#include <functional>

class QLineEdit;

namespace xerahs::app {

class FileUploadResultDialog final : public QDialog {
  Q_OBJECT
public:
  // Success shows the file name, a selectable URL, Copy URL, Open in browser,
  // and the deletion URL when the destination returned one.
  FileUploadResultDialog(const QString &fileName, const uploads::UploadOutcome &outcome, QWidget *parent = nullptr);
  // Failure shows the engine message as plain text. A callback adds the button
  // that opens Upload destinations; the dialog does not know how that window is built.
  FileUploadResultDialog(const QString &fileName, const QString &message, std::function<void()> openDestinations = {},
                         QWidget *parent = nullptr);

private:
  void copyUrl();
  QLineEdit *m_url = nullptr;
};

}  // namespace xerahs::app
