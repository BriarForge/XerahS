// EDITOR-UTILITIES-001 / EU-012, EU-016, EU-017: Video Converter window. It owns its runner, cancels
// a running conversion on close, and takes the runner and the encoder availability as injected ports.
#pragma once

#include "ConversionRunner.h"

#include <QDialog>

#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace xerahs::app {

class VideoConverterDialog final : public QDialog {
  Q_OBJECT
public:
  // Takes ownership of runner; availability must outlive the dialog.
  VideoConverterDialog(media::ConversionRunner *runner, const media::ElementAvailability *availability, QWidget *parent = nullptr);

  void setInput(const QString &path);
  QString input() const;
  QString output() const;
  QString statusText() const;
  bool busy() const;
  void setFolderOpener(std::function<bool(const QString &)> opener) { m_openFolder = std::move(opener); }

public slots:
  void convert();
  void cancel();
  void showInFolder();
  void chooseInput();
  void chooseOutput();

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

private:
  void fillCodecs();
  void updateControls();
  void onFinished(const media::ConversionResult &result);

  media::ConversionRunner *m_runner;
  const media::ElementAvailability *m_availability;
  QLineEdit *m_input, *m_output;
  QComboBox *m_container, *m_codec;
  QSpinBox *m_bitrate;
  QCheckBox *m_audio, *m_overwrite;
  QPushButton *m_chooseInput, *m_chooseOutput, *m_convert, *m_cancel, *m_show, *m_close;
  QProgressBar *m_progress;
  QLabel *m_status;
  bool m_outputEdited = false;
  QString m_lastOutput;
  std::function<bool(const QString &)> m_openFolder;
};

}  // namespace xerahs::app
