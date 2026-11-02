// EC-003: guarded "Open Image from URL" dialog. Nothing is requested until the
// user presses Download. The dialog owns the transfer: Cancel, Escape, close and
// destruction abort it, and the downloaded bytes stay in memory only.
#pragma once
#include "UrlDownload.h"
#include <QDialog>
#include <optional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace xerahs::app {

struct UrlOpenOptions {
  xerahs::editor::DownloadLimits limits;
  QNetworkAccessManager *manager = nullptr;  // injected by tests; the dialog makes its own otherwise
};

struct DownloadedImage {
  QByteArray bytes;
  QUrl finalUrl;
  QString label;  // host[:port] only
};

class UrlOpenDialog final : public QDialog {
  Q_OBJECT
public:
  explicit UrlOpenDialog(QWidget *parent = nullptr, UrlOpenOptions options = {});
  ~UrlOpenDialog() override;
  std::optional<DownloadedImage> takeResult();
  void reject() override;
  // The plain statement of what the user is agreeing to; also used in tests.
  static QString privacyText();

private:
  void begin();
  void finish(const UrlDownloadResult &result);
  void setBusy(bool busy);

  UrlOpenOptions m_options;
  QNetworkAccessManager *m_manager = nullptr;
  UrlDownload *m_download = nullptr;
  QLineEdit *m_url = nullptr;
  QCheckBox *m_allowHttp = nullptr;
  QLabel *m_status = nullptr;
  QProgressBar *m_progress = nullptr;
  QPushButton *m_start = nullptr, *m_cancel = nullptr;
  std::optional<DownloadedImage> m_result;
  bool m_requestedHttp = false;
};
}  // namespace xerahs::app
