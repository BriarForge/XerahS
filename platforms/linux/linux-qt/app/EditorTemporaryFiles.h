#pragma once
#include <QString>
#include <QDateTime>
namespace xerahs::app {
// Owner-private managed preparation/upload files. QTemporaryFile removes them
// at task end; startup unlinks abandoned files older than 24 hours. Unlink is
// best effort deletion, not a promise of physical erasure on SSD/COW storage.
QString editorTemporaryDirectory();
QString editorTemporaryTemplate();
bool sweepEditorTemporaryFiles(const QString &directory, const QDateTime &now = QDateTime::currentDateTimeUtc());
}
