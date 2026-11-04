// INTERFACE-CATALOG-001: screen colour picker (EDITOR-UTILITIES-001
// screen-color-picker). Text formats come from tools/ColorFormats.h.
#pragma once

#include "PortalColorPicker.h"
#include "PortalScreenshot.h"
#include <QPointer>
#include <functional>

#include <QColor>
#include <QDialog>
#include <QList>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace xerahs::app {

class ScreenColorSampler;
struct ColorPickerPorts { std::function<PortalScreenshot *(QObject *)> screenshot; };
class ColorPickerDialog final : public QDialog {
  Q_OBJECT

public:
  explicit ColorPickerDialog(QWidget *parent = nullptr, ColorPickerPorts ports = {});
  ~ColorPickerDialog() override;
  void reject() override;
  void showCapturedSnapshot(const QImage &image);

  // Shows one colour and remembers it in this dialog's recent list.
  void showColor(const QColor &color);
  // Asks the desktop portal to pick. One request at a time.
  void pick();
  // Portal completion. Cancel leaves the shown colour and any message as they are.
  void notePortalResult(PortalColorPicker::Outcome outcome, const QColor &color);

  QColor color() const { return m_color; }

private:
  void applyColor(const QColor &color);
  void remember(const QColor &color);
  void refreshRecent();
  void commitHex();
  void copyLine(int index);
  void copyAll();
  void setCopyEnabled(bool enabled);
  void showStatus(const QString &message);

  QColor m_color;
  QList<QColor> m_recentColors;
  bool m_applying = false;
  ColorPickerPorts m_ports;
  QPointer<PortalScreenshot> m_capture;
  QPointer<ScreenColorSampler> m_sampler;
  quint64 m_captureGeneration = 0;
  QLabel *m_coordinates = nullptr;
  QLabel *m_swatch = nullptr;
  QLineEdit *m_hex = nullptr;
  QLabel *m_hexError = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_lines[8] = {};
  QPushButton *m_copies[8] = {};
  QPushButton *m_copyAll = nullptr;
  QPushButton *m_pickAgain = nullptr;
  QListWidget *m_recent = nullptr;
};

}  // namespace xerahs::app
