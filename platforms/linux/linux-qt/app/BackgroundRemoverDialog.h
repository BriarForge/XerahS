// EDITOR-UTILITIES-001 / EU-009, EU-010: the Background Remover window's
// model supply. EU-011 (inference, preview and save) is not in this window
// yet: the approved runtime lands separately and the status rows say so.
// Every disclosure the contract demands is on screen before anything is
// downloaded, the download is an explicit opt-in, and the bytes are verified
// against the catalogue pins before they can enter the model store.
#pragma once

#include "background/BackgroundModels.h"

#include <QDialog>
#include <QList>
#include <QString>

#include <functional>

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

  // EU-009 status rows (also read by tests).
  QString modelStatus() const;
  QString deviceStatus() const;
  QString setupStatus() const;
  QString inferenceStatus() const;
  QString errorText() const;
  bool downloading() const { return m_reply != nullptr; }

public slots:
  void downloadModel();      // explicit opt-in only; never automatic
  void cancelDownload();
  void chooseLocalModel();

protected:
  void reject() override;    // a busy window cancels its download on close

private:
  void buildUi();
  void refreshModelDisclosure();
  void refreshStoreValidation();
  void setSetupStatus(const QString &text);
  void showError(const QString &message);
  void clearError();
  const bg::CatalogueEntry *currentEntry() const;
  QString defaultStoreDirectory() const;

  QList<bg::CatalogueEntry> m_entries;
  QString m_storeDir;
  QString m_localModelPath;   // empty: no user-supplied model selected
  ModelFilePicker m_picker;
  DownloadConfirm m_confirm;

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

  QNetworkAccessManager *m_net = nullptr;
  QNetworkReply *m_reply = nullptr;
  QString m_downloadTemp;
  bool m_abandoned = false;   // the download was discarded mid-flight (size lie)
};

}  // namespace xerahs::app
