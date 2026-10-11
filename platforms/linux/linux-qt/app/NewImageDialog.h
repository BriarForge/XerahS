// EC-001: native creation controls and cancellable preparation outside history.
#pragma once
#include "NewImageSource.h"
#include "RasterSource.h"
#include <QDialog>
#include <QTimer>
#include <atomic>
#include <future>
#include <memory>
#include <variant>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace xerahs::app {
class NewImageDialog final : public QDialog {
  Q_OBJECT
public:
  explicit NewImageDialog(QWidget *parent = nullptr);
  ~NewImageDialog() override;
  std::optional<PreparedNewImage> takeImage();
  // Shared entry points used by the native file picker and clipboard action.
  bool loadInitialFile(const QString &path);
  bool setInitialImage(const QImage &image, const QString &label);
  void accept() override;
  void reject() override;
private:
  void validateControls();
  void chooseFile();
  void useClipboard();
  void chooseColour();
  bool startWork(const QString &path, bool load);
  void pollWork();
  void setBusy(bool busy);
  xerahs::editor::NewImageOptions options() const;
  struct WorkControl { std::atomic<bool> cancelled{false}; std::atomic<int> progress{0}; };
  using WorkResult = std::variant<LoadedRaster, PreparedNewImage>;
  QWidget *m_controls;
  QSpinBox *m_width, *m_height;
  QComboBox *m_background, *m_profile;
  QLineEdit *m_colour;
  QPushButton *m_chooseColour, *m_clearInitial, *m_cancelWork;
  QDialogButtonBox *m_buttons;
  QLabel *m_initialLabel, *m_status;
  QProgressBar *m_progress;
  QImage m_initial;
  QTimer m_poll;
  std::shared_ptr<WorkControl> m_control;
  std::future<WorkResult> m_worker;
  std::optional<PreparedNewImage> m_prepared;
  QString m_initialName;
  bool m_loading = false, m_closing = false;
};
}  // namespace xerahs::app
