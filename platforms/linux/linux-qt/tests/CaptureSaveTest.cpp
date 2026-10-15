#include "CaptureSave.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <future>
#include <unistd.h>
using namespace xerahs::app;
namespace {
QImage pixels() { QImage image(12, 8, QImage::Format_ARGB32); image.fill(QColor(23, 45, 67, 255)); return image; }
QByteArray bytes(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
bool write(const QString &path, const QByteArray &data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); }
QStringList entries(const QTemporaryDir &dir) { return QDir(dir.path()).entryList(QDir::AllEntries | QDir::System | QDir::Hidden | QDir::NoDotAndDotDot); }
}
class CaptureSaveTest final : public QObject {
  Q_OBJECT
private slots:
  void publishesCompletePrivatePngAndRecordsActualArtifactAndContext() {
    QTemporaryDir dir; const auto source = pixels(); const auto before = source.copy();
    const auto result = saveCapture(source, dir.path(), "capture-%i"); QVERIFY(result.file); QVERIFY(result.diagnostic.isEmpty());
    const auto &file = *result.file; QVERIFY(QDir::isAbsolutePath(file.path)); QCOMPARE(QFileInfo(file.path).fileName(), QString("capture-1.png"));
    QCOMPARE(file.mediaType, QString("image/png")); QCOMPARE(file.byteLength, qint64(bytes(file.path).size())); QVERIFY(file.byteLength > 0);
    QCOMPARE(QImage(file.path), source); QCOMPARE(source, before);
    QCOMPARE(QFile::permissions(file.path) & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther), QFile::Permissions{});
    QCOMPARE(file.filenameContext.value("width").toInt(), 12); QCOMPARE(file.filenameContext.value("height").toInt(), 8);
    QCOMPARE(file.filenameContext.value("counter").toInt(), 0); QCOMPARE(file.filenameContext.value("next_counter").toInt(), 1);
    QCOMPARE(file.filenameContext.value("pattern").toString(), QString("capture-%i"));
    QCOMPARE(entries(dir), QStringList{"capture-1.png"});
  }
  void collisionsIncludingDanglingSymlinksNeverReplaceExistingData() {
    QTemporaryDir dir; const auto path = dir.filePath("capture.png"); QVERIFY(write(path, "unrelated user data"));
    auto result = saveCapture(pixels(), dir.path(), "capture"); QVERIFY(!result.file); QCOMPARE(result.diagnostic, QString("save-name-collision"));
    QCOMPARE(bytes(path), QByteArray("unrelated user data")); QCOMPARE(entries(dir), QStringList{"capture.png"});
    QVERIFY(QFile::remove(path)); const auto target = QFile::encodeName(dir.filePath("missing")); const auto link = QFile::encodeName(path);
    QVERIFY(::symlink(target.constData(), link.constData()) == 0);
    result = saveCapture(pixels(), dir.path(), "capture"); QVERIFY(!result.file); QCOMPARE(result.diagnostic, QString("save-name-collision"));
    QVERIFY(QFileInfo(path).isSymLink()); QVERIFY(!QFileInfo::exists(dir.filePath("missing"))); QCOMPARE(entries(dir), QStringList{"capture.png"});
  }
  void destinationCreatedImmediatelyBeforePublicationIsPreservedAndRandomNameRetries() {
    QTemporaryDir dir; QString raced; int encodings = 0, publications = 0;
    const auto result = saveCapture(pixels(), dir.path(), "capture-%ra{20}", {
      [&](const QImage &image, QIODevice *file) { ++encodings; return image.save(file, "PNG"); },
      [&](const QString &path) { if (++publications == 1) { raced = path; if (!write(path, "race winner")) throw 1; } }
    });
    QVERIFY(result.file); QCOMPARE(encodings, 1); QCOMPARE(publications, 2);
    QCOMPARE(bytes(raced), QByteArray("race winner")); QVERIFY(result.file->path != raced); QCOMPARE(QImage(result.file->path), pixels());
    QCOMPARE(entries(dir).size(), 2);
  }
  void concurrentSavesWithTheSameNameHaveOneWinnerAndNoPartialFinalFile() {
    QTemporaryDir dir; auto source = pixels();
    auto first = std::async(std::launch::async, [&] { return saveCapture(source, dir.path(), "same"); });
    auto second = std::async(std::launch::async, [&] { return saveCapture(source, dir.path(), "same"); });
    const auto a = first.get(), b = second.get(); QCOMPARE(bool(a.file) + bool(b.file), 1);
    QCOMPARE(a.file ? b.diagnostic : a.diagnostic, QString("save-name-collision"));
    QCOMPARE(QImage(dir.filePath("same.png")), source); QCOMPARE(entries(dir), QStringList{"same.png"});
  }
  void encoderFailureAndExceptionsRemoveOnlyOwnedTemporaryFiles() {
    QTemporaryDir dir; QVERIFY(write(dir.filePath("keep.txt"), "keep"));
    auto result = saveCapture(pixels(), dir.path(), "capture", {[](const QImage &, QIODevice *file) { file->write("partial"); return false; }, {}});
    QVERIFY(!result.file); QCOMPARE(result.diagnostic, QString("save-write-failed")); QCOMPARE(entries(dir), QStringList{"keep.txt"});
    result = saveCapture(pixels(), dir.path(), "capture", {{}, [](const QString &) { throw std::runtime_error("private value"); }});
    QVERIFY(!result.file); QCOMPARE(result.diagnostic, QString("save-write-failed")); QCOMPARE(entries(dir), QStringList{"keep.txt"});
    QCOMPARE(bytes(dir.filePath("keep.txt")), QByteArray("keep"));
  }
  void invalidPatternDoesNotCreateDirectoryOrEncode() {
    QTemporaryDir dir; int encodes = 0;
    const auto result = saveCapture(pixels(), dir.filePath("absent/nested"), "%ra{0}", {[&](const QImage &, QIODevice *) { ++encodes; return false; }, {}});
    QVERIFY(!result.file); QCOMPARE(result.diagnostic, QString("filename-invalid-argument")); QCOMPARE(encodes, 0); QVERIFY(entries(dir).isEmpty());
  }
};
QTEST_GUILESS_MAIN(CaptureSaveTest)
#include "CaptureSaveTest.moc"
