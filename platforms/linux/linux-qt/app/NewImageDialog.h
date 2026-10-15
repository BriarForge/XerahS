// EC-001: native creation controls and cancellable preparation outside history.
#pragma once
#include "NewImageSource.h"
#include "RasterSource.h"
#include <QDialog>
#include <QTimer>
#include <QPointer>
#include <atomic>
#include <functional>
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
struct NewImageHooks { std::function<void()> beforePrepare; }; // worker-only owned captures
class NewImageDialog final : public QDialog {
  Q_OBJECT
public:
  explicit NewImageDialog(QWidget *parent = nullptr, NewImageHooks hooks = {});
  ~NewImageDialog() override;
  std::optional<PreparedNewImage> takeImage();
  // Shared entry points used by the native file picker and clipboard action.
  bool loadInitialFile(const QString &path);
  bool setInitialImage(const QImage &image, const QString &label);
  void accept() override;
  void reject() override;
  void done(int result) override;
private:
  void validateControls();
  QString controlStatus() const;
  void clearInitial();
  void publishInitial(QImage image, const QString &label);
  void cancelWork();
  void chooseFile();
  void useClipboard();
  void chooseColour();
  bool startWork(const QString &path, bool load);
  void pollWork();
  void setBusy(bool busy);
  xerahs::editor::NewImageOptions options() const;
  using WorkResult = std::variant<LoadedRaster, PreparedNewImage>;
  struct Work {
    std::atomic<bool> cancelled{false}, ready{false};
    std::atomic<int> progress{0};
    WorkResult result;
    bool load = false;
    QString label;
  };
  const NewImageHooks m_hooks;
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
  std::shared_ptr<Work> m_work;
  QPointer<QDialog> m_picker;
  std::optional<PreparedNewImage> m_prepared;
  bool m_closing = false, m_transition = false, m_pickerOpen = false;
};
}  // namespace xerahs::app
