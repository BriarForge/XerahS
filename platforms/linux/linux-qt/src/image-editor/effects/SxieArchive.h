// COMPAT-SXIE-001, EE-015, ROOT-SECURITY-001: bounded, memory-only ZIP data.
#pragma once
#include <QByteArray>
#include <QMap>
#include <QString>
namespace xerahs::effects {
struct SxieArchive {
  QMap<QString, QByteArray> entries;
  QString error;
  bool ok() const { return error.isEmpty(); }
};
SxieArchive readSxieArchive(const QByteArray &bytes);
}
