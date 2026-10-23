// EDITOR-UTILITIES-001 / EU-003, EU-015..EU-017: the QR code generator window.
// Content never leaves the machine except through an explicit Copy, Save or
// Upload; Upload hands a PNG path to the host's file-upload port.
#pragma once

#include "QrRender.h"

#include <QDialog>
#include <QImage>
#include <QTemporaryDir>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

namespace xerahs::app {

class QrCodeDialog final : public QDialog {
  Q_OBJECT
public:
  explicit QrCodeDialog(QWidget *parent = nullptr);

  // The host connects this to its file uploader. Unset disables Upload.
  void setUploadPort(std::function<void(const QString &pngPath)> port);

  void setText(const QString &text);
  void setEcc(tools::QrEcc ecc);
  void setImageSize(int pixels);

  QString text() const;
  // The current preview; null while the input is invalid.
  QImage image() const { return m_image; }
  tools::QrMatrix matrix() const { return m_matrix; }
  // Typed code of the current failure ("qr-empty-content", ...), empty when valid.
  QString errorCode() const { return m_error; }
  QString statusText() const;

public slots:
  void copyImage();
  // Atomic: written to a temporary sibling and renamed (QSaveFile).
  bool saveTo(const QString &path);
  void chooseAndSave();
  void upload();

private:
  void refresh();
  void fail(const QString &code, const QString &message);
  void setActionsEnabled(bool enabled);

  QPlainTextEdit *m_text = nullptr;
  QComboBox *m_ecc = nullptr;
  QSpinBox *m_size = nullptr;
  QLabel *m_preview = nullptr;
  QLabel *m_status = nullptr;
  QPushButton *m_copy = nullptr, *m_save = nullptr, *m_upload = nullptr;
  std::function<void(const QString &)> m_uploadPort;
  QImage m_image;
  tools::QrMatrix m_matrix;
  QString m_error;
  // EU-017: uploaded PNGs live here and are removed with the dialog.
  std::unique_ptr<QTemporaryDir> m_temp;
  int m_uploadCount = 0;
};

}  // namespace xerahs::app
