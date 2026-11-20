// EDITOR-UTILITIES-001 / EU-009, EU-010, EU-011: the Background Remover
// window. The model supply is disclosed and explicit (EU-010); inference,
// preview and save run the approved engine off the GUI thread with
// cancellation and the EU-011 failure split. The engine is optional at
// build time: without it the window reports it as not linked and everything
// else still works.
#pragma once

#include "background/BackgroundModels.h"
#include "OwnedWork.h"

#include <QDialog>
#include <QImage>
#include <QList>
#include <QString>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

class QNetworkAccessManager;
class QNetworkReply;

namespace xerahs::app {

class BackgroundRemoverDialog final : public QDialog {
  Q_OBJECT
public:
  // Replaces the native open-file dialog, so tests inject a path and the
  // shipped build shows a QFileDialog.
  using ModelFilePicker = std::function<QString()>;
  // The explicit opt-in seam. The default implementation shows a QMessageBox
  // with the full EU-010 disclosure; tests inject a decision.
  using DownloadConfirm = std::function<bool(const bg::CatalogueEntry &)>;

  explicit BackgroundRemoverDialog(QWidget *parent = nullptr);

  void setCatalogue(const QList<bg::CatalogueEntry> &entries);
  void setStoreDirectory(const QString &directory);   // tests inject a temp store
  void setModelFilePicker(ModelFilePicker picker);
  void setDownloadConfirm(DownloadConfirm confirm);
  // Tests inject a fake engine; the default is the approved onnxruntime
  // engine, or nullptr in a build without the optional runtime.
  void setMaskEngine(std::shared_ptr<bg::MaskEngine> engine);
  // The save destination seam: returns a chosen path or an empty string.
  using SaveFilePicker = std::function<QString(const QString &suggestedPath)>;
  void setSaveFilePicker(SaveFilePicker picker);

  // EU-009 status rows (also read by tests).
  QString modelStatus() const;
  QString deviceStatus() const;
  QString setupStatus() const;
  QString inferenceStatus() const;
  QString errorText() const;
  bool downloading() const { return m_reply != nullptr; }
  bool inferring() const { return m_inferenceWork.running(); }
  QImage resultImage() const { return m_result; }

public slots:
  void downloadModel();      // explicit opt-in only; never automatic
  void cancelDownload();
  void chooseLocalModel();
  void chooseImage();
  void removeBackground();
  void cancelInference();
  void saveResult();

protected:
  void reject() override;    // a busy window cancels its work on close

private:
  void buildUi();
  void refreshModelDisclosure();
  void refreshStoreValidation();
  void refreshEngineStatus();
  void setSetupStatus(const QString &text);
  void setInferenceStatus(const QString &text);
  void showError(const QString &message);
  void clearError();
  const bg::CatalogueEntry *currentEntry() const;
  QString defaultStoreDirectory() const;

  QList<bg::CatalogueEntry> m_entries;
  QString m_storeDir;
  QString m_localModelPath;   // empty: no user-supplied model selected
  ModelFilePicker m_picker;
  DownloadConfirm m_confirm;
  SaveFilePicker m_savePicker;
  std::shared_ptr<bg::MaskEngine> m_engine;
  QImage m_source, m_result;
  QString m_sourcePath;
  OwnedWork m_inferenceWork;

  QComboBox *m_modelCombo = nullptr;
  QLabel *m_description = nullptr;
  QLabel *m_upstream = nullptr;
  QLabel *m_licence = nullptr;
  QLabel *m_size = nullptr;
  QLabel *m_sourceUrl = nullptr;
  QLabel *m_host = nullptr;
  QLabel *m_checksums = nullptr;
  QLabel *m_storeValue = nullptr;
  QLabel *m_network = nullptr;
  QPushButton *m_download = nullptr;
  QPushButton *m_cancelDownload = nullptr;
  QPushButton *m_chooseLocal = nullptr;
  QProgressBar *m_progress = nullptr;
  QLabel *m_modelStatus = nullptr;
  QLabel *m_deviceStatus = nullptr;
  QLabel *m_setupStatus = nullptr;
  QLabel *m_inferenceStatus = nullptr;
  QLabel *m_error = nullptr;

  QLabel *m_sourcePathLabel = nullptr;
  QComboBox *m_deviceCombo = nullptr;
  QPushButton *m_chooseImage = nullptr;
  QPushButton *m_remove = nullptr;
  QPushButton *m_cancelInference = nullptr;
  QPushButton *m_save = nullptr;
  QLabel *m_resultPreview = nullptr;

  QNetworkAccessManager *m_net = nullptr;
  QNetworkReply *m_reply = nullptr;
  QString m_downloadTemp;
  bool m_abandoned = false;   // the download was discarded mid-flight (size lie)
};

}  // namespace xerahs::app
