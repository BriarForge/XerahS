#pragma once
#include "ImageRecognition.h"
#include "OwnedWork.h"
#include <QDialog>
#include <QPointer>
#include <functional>
#include <optional>
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
namespace xerahs::app {
class PortalScreenshot;
struct RecognitionImageChoice { QImage image; QString diagnostic; bool cancelled = false; };
struct ImageRecognitionPorts {
  // GUI-only consent/picker hooks. Defaults use native file and portal surfaces.
  std::function<QString(QWidget *)> chooseFile;
  std::function<void(std::function<void(RecognitionImageChoice)>)> capture;
  std::function<void()> cancelCapture;
  std::function<RecognitionImageChoice(const QImage &, QWidget *)> selectRegion;
  // Worker-only; arguments and captures must outlive a cancelled/closed dialog.
  std::function<ImageRecognitionResult(ImageRecognitionKind, const QImage &, const xerahs::editor::RenderControl &)> recognize;
};
class ImageRecognitionDialog final : public QDialog {
  Q_OBJECT
public:
  explicit ImageRecognitionDialog(ImageRecognitionKind, QWidget *parent = nullptr, ImageRecognitionPorts = {});
  ~ImageRecognitionDialog() override;
  QStringList values() const { return m_values; }
  bool busy() const { return m_busy; }
  // Adopts already decoded, owned text for a capture result. No image is
  // captured, uploaded or copied; a busy input operation is not replaced.
  void showRecognizedValues(const QStringList &values);
public slots:
  void scanFile();
  void scanScreen();
  void scanRegion();
  void cancel();
  void reject() override;
signals:
  void recognitionFinished(const QStringList &values, const QString &diagnostic, bool cancelled);
private:
  void capture(bool region);
  void acquired(quint64 generation, bool region, RecognitionImageChoice);
  void recognize(QImage, quint64 generation);
  void complete(quint64 generation, ImageRecognitionResult);
  void setBusy(bool);
  void cancelWork(bool announce);
  ImageRecognitionKind m_kind;
  ImageRecognitionPorts m_ports;
  OwnedWork m_work;
  QPointer<PortalScreenshot> m_portal;
  QStringList m_values;
  QLabel *m_status = nullptr;
  QPlainTextEdit *m_results = nullptr;
  QProgressBar *m_progress = nullptr;
  QPushButton *m_file = nullptr, *m_screen = nullptr, *m_region = nullptr, *m_cancel = nullptr, *m_copy = nullptr;
  quint64 m_generation = 0;
  bool m_busy = false, m_available = false;
};
} // namespace xerahs::app
