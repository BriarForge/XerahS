#pragma once
#include "ImageInsert.h"
#include "OwnedWork.h"
#include <QDialog>
#include <QPointer>
#include <functional>
class QFileDialog;
class QLabel;
class QPushButton;
class QProgressBar;
namespace xerahs::app {
struct ImageInsertHooks { std::function<void()> beforePrepare; };
class ImageInsertDialog final : public QDialog {
  Q_OBJECT
public:
  explicit ImageInsertDialog(QWidget *parent = nullptr, ImageInsertHooks hooks = {});
  ~ImageInsertDialog() override;
  const PreparedImageAsset &choice() const { return m_prepared; }
  bool busy() const { return m_busy; }
  // Both native sources pass through exactly the same worker validation.
  bool loadFile(const QString &path);
public slots:
  void useClipboard();
  void reject() override;
  void done(int result) override;
private:
  void chooseFile();
  void prepare(QString path, QImage owned);
  void complete(quint64 generation, PreparedImageAsset);
  void setBusy(bool);
  void cancelWork();
  ImageInsertHooks m_hooks;
  PreparedImageAsset m_prepared;
  OwnedWork m_work;
  QPointer<QFileDialog> m_picker;
  QLabel *m_status = nullptr;
  QPushButton *m_file = nullptr, *m_clipboard = nullptr;
  QProgressBar *m_progress = nullptr;
  quint64 m_generation = 0;
  bool m_busy = false;
};
} // namespace xerahs::app
