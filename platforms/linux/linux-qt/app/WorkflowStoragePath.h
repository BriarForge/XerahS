#pragma once
#include <QDir>
#include <QStandardPaths>
namespace xerahs::app {
// Shared native GUI/CLI location. Computing it creates no directory or settings.
inline QString nativeCaptureWorkflowPath() {
  const auto directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  return directory.isEmpty() ? QString{} : QDir(directory).filePath("com.xerahs.app/linux-qt/capture-workflows.json");
}
} // namespace xerahs::app
