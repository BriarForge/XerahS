// EDITOR-UTILITIES-001 / EU-007: the icon converter window.
#pragma once

#include "IconWriter.h"

#include <QDialog>
#include <QImage>
#include <QList>
#include <QString>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace xerahs::app {

class IconConverterDialog final : public QDialog {
  Q_OBJECT
public:
  // Returns the chosen destination, or an empty string for a cancel.
  using FileChooser = std::function<QString(const QString &suggestedPath)>;

  explicit IconConverterDialog(QWidget *parent = nullptr);

  // Replaces the native save dialog, so the caller owns the destination.
  void setFileChooser(FileChooser chooser);
  // Loads and validates the image at path. An unusable image reports an error.
  void setImage(const QString &path);

  QString imagePath() const;
  QString errorText() const;
  IconOptions options() const;
  // The six size boxes, in ascending size order.
  QList<QCheckBox *> sizeCheckBoxes() const;
  QList<int> selectedSizes() const;

public slots:
  void chooseImage();
  void save();

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  void updatePreview();
  void updateSaveState();
  void showError(const QString &message);
  void clearError();
  IconOptions::Mode mode() const;
  QString suggestedPath() const;

  QLineEdit *m_path = nullptr;
  QPushButton *m_choose = nullptr;
  QList<QCheckBox *> m_sizes;
  QRadioButton *m_trueColor = nullptr;
  QRadioButton *m_palette = nullptr;
  QLabel *m_preview = nullptr;
  QPushButton *m_save = nullptr;
  QLabel *m_error = nullptr;
  QLabel *m_status = nullptr;
  QImage m_source;
  FileChooser m_chooser;
};

}  // namespace xerahs::app
