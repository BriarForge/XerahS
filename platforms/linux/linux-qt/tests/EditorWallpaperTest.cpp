// EO-010 on a private session bus with a fake org.freedesktop.portal.Wallpaper.
// Every directory, image and portal belongs to this fixture.
#include "EditorWallpaper.h"
#include "EditorWindow.h"
#include "EditorSource.h"
#include "EditorCanvas.h"
#include "NativeEditorPreferences.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <memory>

using namespace xerahs::app;
using namespace xerahs::editor;
using namespace xerahs::actions;
namespace {
const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
const QString root = QStringLiteral("/org/freedesktop/portal/desktop");
LoadedSource fixture() {
  QImage image(40, 20, QImage::Format_ARGB32); image.setColorSpace(QColorSpace::SRgb); image.fill(qRgba(255, 0, 0, 128));
  auto source = prepareCapturedSource(image); RectangleAnnotation rectangle;
  rectangle.id = QUuid::createUuid(); rectangle.start = {20, 0}; rectangle.end = {40, 20};
  rectangle.style.fillColor = 0xFF0000FF; rectangle.style.strokeColor = 0xFF0000FF; rectangle.style.strokeWidth = 1;
  source.document->annotations = {rectangle}; return source;
}
QByteArray bytes(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
QStringList files(const QString &dir) { return QDir(dir).entryList(QDir::Files | QDir::NoDotAndDotDot); }
EditorWallpaperPorts confirmed(const QString &dir, PortalWallpaper::Target target = PortalWallpaper::Target::Background, bool preview = true) {
  EditorWallpaperPorts ports; ports.directory = dir;
  ports.confirm = [target, preview](const EditorWallpaperSnapshot &, QWidget *) { return EditorWallpaperChoice{ActionOutcome{}, target, preview}; };
  return ports;
}
}
class FakeRequest final : public QObject {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")
public:
  FakeRequest(QString path, QStringList &closed, QObject *parent) : QObject(parent), path(std::move(path)), closed(closed) {}
  QString path; QStringList &closed;
public slots:
  void Close() { closed.append(path); }
};
class FakeWallpaperPortal final : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Wallpaper")
public:
  explicit FakeWallpaperPortal(QDBusConnection bus) : bus(std::move(bus)) {
    if (!this->bus.registerObject(root, this, QDBusConnection::ExportAllSlots)) qFatal("Fixture portal registration failed");
  }
  ~FakeWallpaperPortal() override { bus.unregisterObject(root); }
  QDBusConnection bus;
  int calls = 0; uint code = 0; bool hang = false; QString error;
  QString parentWindow, handle; QVariantMap options; QByteArray received; QStringList closed;
public slots:
  QDBusObjectPath SetWallpaperFile(const QString &parent, const QDBusUnixFileDescriptor &descriptor, const QVariantMap &requested) {
    ++calls; parentWindow = parent; options = requested;
    if (!error.isEmpty()) { sendErrorReply(error, QStringLiteral("Private fixture details must not escape")); return QDBusObjectPath("/"); }
    QFile file; if (descriptor.isValid() && file.open(descriptor.fileDescriptor(), QIODevice::ReadOnly, QFile::DontCloseHandle)) received = file.readAll();
    QString sender = message().service().mid(1); sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    handle = root + "/request/" + sender + "/" + requested.value("handle_token").toString();
    if (!bus.registerObject(handle, new FakeRequest(handle, closed, this), QDBusConnection::ExportAllSlots)) qFatal("Fixture request registration failed");
    if (!hang) QTimer::singleShot(10, this, [this, path = handle, answer = code] {
      auto signal = QDBusMessage::createSignal(path, "org.freedesktop.portal.Request", "Response");
      signal << answer << QVariantMap{}; bus.send(signal);
    });
    return QDBusObjectPath(handle);
  }
};
class EditorWallpaperTest final : public QObject {
  Q_OBJECT
  QTemporaryDir settings;
  QDBusConnection server{QString()};
private slots:
  void initTestCase() {
    QVERIFY(settings.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
  }
  void init() {
    QVERIFY(initializeNativeEditorPreferences(settings.filePath("editor.json")).isEmpty());
    server = QDBusConnection::connectToBus(QDBusConnection::SessionBus, "wallpaper-test-server");
    QVERIFY(server.isConnected()); QVERIFY(server.registerService(service));
  }
  void cleanup() {
    server.unregisterService(service); QDBusConnection::disconnectFromBus("wallpaper-test-server");
    QThreadPool::globalInstance()->waitForDone();
  }
  void confirmedWallpaperPassesRetainedFileAndLeavesDocumentUnchanged() {
    QTemporaryDir dir; FakeWallpaperPortal portal(server);
    const auto source = fixture(); std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedDocument(*source.document)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas);
    QTest::mousePress(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({2, 2}).toPoint());
    QTest::mouseRelease(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->viewState().toView({8, 8}).toPoint());
    window->findChild<QAction *>("undo")->trigger();
    const auto title = window->windowTitle(); const auto document = serializeDocument(canvas->annotationDocument()); const auto state = canvas->annotationState();
    QVERIFY(window->findChild<QAction *>("redo")->isEnabled());
    window->setWallpaperPorts(confirmed(dir.path(), PortalWallpaper::Target::Both, false));
    QSignalSpy finished(window.get(), &EditorWindow::wallpaperFinished), saved(window.get(), &EditorWindow::saveFinished);
    auto *action = window->findChild<QAction *>("setWallpaper"); QVERIFY(action && action->isEnabled());
    QCOMPARE(action->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_W)); QVERIFY(!action->autoRepeat());
    action->trigger();
    QCOMPARE(portal.calls, 1); QCOMPARE(portal.options.value("set-on").toString(), QString("both"));
    QCOMPARE(portal.options.value("show-preview").toBool(), false); QVERIFY(portal.options.value("handle_token").toString().startsWith("xerahs"));
    QCOMPARE(finished.count(), 1); QCOMPARE(finished[0][0].toString(), QString("succeeded")); QCOMPARE(finished[0][1].toString(), QString());
    const auto path = finished[0][2].toString(); QCOMPARE(QFileInfo(path).absolutePath(), dir.path()); QVERIFY(QFileInfo(path).fileName().startsWith("wallpaper-"));
    QCOMPARE(files(dir.path()), QStringList{QFileInfo(path).fileName()});  // no temporary leftovers
    QVERIFY(!portal.received.isEmpty()); QCOMPARE(portal.received, bytes(path));
    QImage png; QVERIFY(png.loadFromData(portal.received, "PNG")); QCOMPARE(png.size(), QSize(40, 20)); QVERIFY(png.hasAlphaChannel());
    QCOMPARE(png.pixelColor(5, 5), QColor::fromRgba(qRgba(255, 0, 0, 128))); QCOMPARE(png.pixel(30, 10), qRgb(0, 0, 255));
    const auto permissions = QFile::permissions(path);
    QVERIFY(permissions & QFileDevice::ReadOwner); QVERIFY(permissions & QFileDevice::WriteOwner);
    QVERIFY(!(permissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)));
    const auto message = window->statusBar()->currentMessage(); QVERIFY(message.contains(path)); QVERIFY(message.contains("all displays"));
    QCOMPARE(window->windowTitle(), title); QCOMPARE(saved.count(), 0); QCOMPARE(serializeDocument(canvas->annotationDocument()), document);
    QCOMPARE(canvas->annotationState(), state); QVERIFY(window->findChild<QAction *>("redo")->isEnabled()); QVERIFY(action->isEnabled());
  }
  void nativeDialogShowsChoiceAndPathAndAppliesLockscreen_data() {
    QTest::addColumn<bool>("shortcut"); QTest::newRow("menu") << false; QTest::newRow("shortcut") << true;
  }
  void nativeDialogShowsChoiceAndPathAndAppliesLockscreen() {
    QFETCH(bool, shortcut);
    QTemporaryDir dir; FakeWallpaperPortal portal(server);
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show();
    EditorWallpaperPorts ports; ports.directory = dir.path(); window->setWallpaperPorts(ports);
    QString text, note; bool sawPath = false; int choices = 0; QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&] {
      auto *dialog = window->findChild<QDialog *>("confirmEditorWallpaper"); if (!dialog || !dialog->isVisible()) return;
      timer.stop(); QVERIFY(portal.calls == 0); QVERIFY(files(dir.path()).isEmpty());  // nothing is written before confirmation
      auto *combo = dialog->findChild<QComboBox *>("wallpaperTarget"); choices = combo->count(); combo->setCurrentIndex(1);
      dialog->findChild<QCheckBox *>("wallpaperShowPreview")->setChecked(false);
      note = dialog->findChild<QLabel *>("wallpaperDisplayNote")->text(); text = dialog->findChild<QLabel *>("wallpaperFilePath")->text();
      sawPath = text.contains(dir.path());
      auto *buttons = dialog->findChild<QDialogButtonBox *>(); QCOMPARE(buttons->button(QDialogButtonBox::Cancel)->isDefault(), true);
      buttons->button(QDialogButtonBox::Ok)->click();
    });
    QSignalSpy finished(window.get(), &EditorWindow::wallpaperFinished);
    auto *canvas = window->findChild<EditorCanvas *>(); QVERIFY(canvas);
    if (shortcut) { window->activateWindow(); canvas->setFocus(); QTRY_VERIFY(window->isActiveWindow()); }
    timer.start(2);
    if (shortcut) QTest::keyClick(canvas, Qt::Key_W, Qt::ControlModifier | Qt::AltModifier);
    else window->findChild<QAction *>("setWallpaper")->trigger();
    timer.stop();
    QCOMPARE(choices, 3); QVERIFY(sawPath); QVERIFY(note.contains("all displays")); QVERIFY(note.contains("no per-display"));
    QCOMPARE(portal.calls, 1); QCOMPARE(portal.options.value("set-on").toString(), QString("lockscreen")); QCOMPARE(portal.options.value("show-preview").toBool(), false);
    QCOMPARE(finished.count(), 1); QCOMPARE(finished[0][0].toString(), QString("succeeded")); QVERIFY(text.contains(finished[0][2].toString()));
  }
  void dialogCancelCallsNoPortalAndWritesNoFile() {
    QTemporaryDir dir; FakeWallpaperPortal portal(server);
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show(); const auto title = window->windowTitle();
    EditorWallpaperPorts ports; ports.directory = dir.path() + "/not-created-yet"; window->setWallpaperPorts(ports); QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&] {
      auto *dialog = window->findChild<QDialog *>("confirmEditorWallpaper"); if (!dialog || !dialog->isVisible()) return;
      timer.stop(); dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
    });
    QSignalSpy finished(window.get(), &EditorWindow::wallpaperFinished); timer.start(2); window->findChild<QAction *>("setWallpaper")->trigger(); timer.stop();
    QCOMPARE(finished.count(), 1); QCOMPARE(finished[0][0].toString(), QString("cancelled")); QCOMPARE(finished[0][2].toString(), QString());
    QCOMPARE(portal.calls, 0); QVERIFY(!QFileInfo::exists(dir.path() + "/not-created-yet")); QCOMPARE(window->windowTitle(), title);
    QVERIFY(window->findChild<QAction *>("setWallpaper")->isEnabled()); QVERIFY(window->statusBar()->currentMessage().contains("not set"));
  }
  void portalCancelAndFailureAreTypedRemoveTheFileAndKeepDocument_data() {
    QTest::addColumn<uint>("code"); QTest::addColumn<QString>("state"); QTest::addColumn<QString>("diagnostic");
    QTest::newRow("portal-cancelled") << 1u << "cancelled" << "";
    QTest::newRow("portal-failed") << 2u << "failed" << "wallpaper-failed";
    QTest::newRow("portal-other") << 7u << "failed" << "wallpaper-failed";
  }
  void portalCancelAndFailureAreTypedRemoveTheFileAndKeepDocument() {
    QFETCH(uint, code); QFETCH(QString, state); QFETCH(QString, diagnostic);
    QTemporaryDir dir; FakeWallpaperPortal portal(server); portal.code = code;
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedDocument(*fixture().document)); QVERIFY(window); window->show();
    auto *canvas = window->findChild<EditorCanvas *>(); const auto document = serializeDocument(canvas->annotationDocument()); const auto title = window->windowTitle();
    window->setWallpaperPorts(confirmed(dir.path())); QSignalSpy finished(window.get(), &EditorWindow::wallpaperFinished);
    window->findChild<QAction *>("setWallpaper")->trigger();
    QCOMPARE(portal.calls, 1); QCOMPARE(finished.count(), 1); QCOMPARE(finished[0][0].toString(), state); QCOMPARE(finished[0][1].toString(), diagnostic);
    QCOMPARE(finished[0][2].toString(), QString()); QVERIFY(files(dir.path()).isEmpty());  // the unused generated file is not left behind
    QCOMPARE(serializeDocument(canvas->annotationDocument()), document); QCOMPARE(window->windowTitle(), title);
    QVERIFY(window->findChild<QAction *>("setWallpaper")->isEnabled()); QVERIFY(window->statusBar()->currentMessage().contains("unchanged") || window->statusBar()->currentMessage().contains("kept"));
  }
  void deniedAndUnsupportedServicesHaveStableCodes() {
    QTemporaryDir dir; std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show();
    window->setWallpaperPorts(confirmed(dir.path())); QSignalSpy finished(window.get(), &EditorWindow::wallpaperFinished);
    {
      FakeWallpaperPortal portal(server); portal.error = "org.freedesktop.portal.Error.NotAllowed";
      window->findChild<QAction *>("setWallpaper")->trigger(); QCOMPARE(portal.calls, 1);
    }
    QCOMPARE(finished.count(), 1); QCOMPARE(finished.last()[1].toString(), QString("wallpaper-permission-denied"));
    {
      FakeWallpaperPortal portal(server); portal.error = "org.freedesktop.DBus.Error.UnknownMethod";  // a portal without the Wallpaper interface
      window->findChild<QAction *>("setWallpaper")->trigger();
    }
    QCOMPARE(finished.count(), 2); QCOMPARE(finished.last()[1].toString(), QString("wallpaper-service-unavailable"));
    QVERIFY(server.unregisterService(service));  // no portal and no activation on the private bus
    window->findChild<QAction *>("setWallpaper")->trigger();
    QCOMPARE(finished.count(), 3); QCOMPARE(finished.last()[0].toString(), QString("failed")); QCOMPARE(finished.last()[1].toString(), QString("wallpaper-service-unavailable"));
    QVERIFY(files(dir.path()).isEmpty()); QVERIFY(!window->statusBar()->currentMessage().contains("Private fixture"));
  }
  void unwritableStorageFailsBeforeAnyPortalCall() {
    QTemporaryDir dir; QFile blocker(dir.filePath("blocker")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
    FakeWallpaperPortal portal(server); std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show();
    window->setWallpaperPorts(confirmed(dir.filePath("blocker/wallpapers"))); QSignalSpy finished(window.get(), &EditorWindow::wallpaperFinished);
    window->findChild<QAction *>("setWallpaper")->trigger();
    QCOMPARE(finished.count(), 1); QCOMPARE(finished[0][0].toString(), QString("failed")); QCOMPARE(finished[0][1].toString(), QString("wallpaper-storage-failed")); QCOMPARE(portal.calls, 0);
  }
  void missingConfirmationServiceDisablesTheAction() {
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show();
    EditorWallpaperPorts ports; ports.confirm = {}; window->setWallpaperPorts(ports); auto *action = window->findChild<QAction *>("setWallpaper");
    QVERIFY(!action->isEnabled()); QVERIFY(action->toolTip().contains("unavailable")); QVERIFY(!QMetaObject::invokeMethod(window.get(), "setWallpaper"));
  }
  void cancellingWhileWaitingClosesRequestAndDisclosesRetainedFile() {
    QTemporaryDir dir; FakeWallpaperPortal portal(server); portal.hang = true;
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show(); window->setWallpaperPorts(confirmed(dir.path()));
    bool cancelled = false; QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&] {
      auto *dialog = window->findChild<QProgressDialog *>("wallpaperApplyProgress"); if (!dialog || portal.calls != 1 || cancelled) return;
      cancelled = true; QVERIFY(!window->findChild<QAction *>("setWallpaper")->isEnabled()); QVERIFY(!window->close());
      dialog->findChild<QPushButton *>()->click();
    });
    QSignalSpy finished(window.get(), &EditorWindow::wallpaperFinished); timer.start(2); window->findChild<QAction *>("setWallpaper")->trigger(); timer.stop();
    QVERIFY(cancelled); QCOMPARE(finished.count(), 1); QCOMPARE(finished[0][0].toString(), QString("cancelled")); QCOMPARE(finished[0][1].toString(), QString("wallpaper-cancel-requested"));
    QTRY_COMPARE(portal.closed, QStringList{portal.handle});  // the portal Request was closed
    QCOMPARE(files(dir.path()).size(), 1); QCOMPARE(finished[0][2].toString(), dir.filePath(files(dir.path()).first()));  // disclosed, because the desktop may have applied it
    QVERIFY(window->statusBar()->currentMessage().contains("may already"));
  }
  void destroyingEditorDuringRequestClosesItAndNeverCallsBack() {
    QTemporaryDir dir; FakeWallpaperPortal portal(server); portal.hang = true;
    QPointer<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show(); window->setWallpaperPorts(confirmed(dir.path()));
    QTimer timer; connect(&timer, &QTimer::timeout, this, [&] { if (portal.calls == 1 && window) delete window.data(); }); timer.start(2);
    window->findChild<QAction *>("setWallpaper")->trigger(); timer.stop();
    QVERIFY(!window); QTRY_COMPARE(portal.closed, QStringList{portal.handle}); QTest::qWait(30);  // late handle replies and responses are harmless
  }
  void destroyingEditorDuringRenderWritesNothing() {
    QTemporaryDir dir; FakeWallpaperPortal portal(server); QPointer<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image)); QVERIFY(window); window->show();
    auto ports = confirmed(dir.path()); std::atomic<bool> entered{false}, released{false}; ports.beforeRender = [&] { entered = true; while (!released) QThread::msleep(1); };
    window->setWallpaperPorts(ports); QTimer timer; connect(&timer, &QTimer::timeout, this, [&] { if (entered && window) delete window.data(); }); timer.start(2);
    window->findChild<QAction *>("setWallpaper")->trigger(); timer.stop(); released = true;
    QVERIFY(!window); QCOMPARE(portal.calls, 0); QTest::qWait(30); QVERIFY(files(dir.path()).isEmpty());
  }
  void visibleUnsupportedAnnotationRefusesLossyWallpaper() {
    auto source = fixture(); const auto id = QUuid::createUuid(); FakeWallpaperPortal portal(server); QTemporaryDir dir;
    source.document->annotations.emplace_back(UnsupportedAnnotation{id, {{"id", id.toString(QUuid::WithoutBraces)}, {"type", "future-shape"}, {"visible", true}}});
    QWidget parent; const auto result = runEditorWallpaper(source.image, *source.document, &parent, confirmed(dir.path()));
    QCOMPARE(result.action.state, ActionState::Failed); QCOMPARE(result.action.diagnostic, std::optional<QString>{"wallpaper-annotation-unsupported"});
    QVERIFY(!result.portalCalled); QCOMPARE(portal.calls, 0); QVERIFY(files(dir.path()).isEmpty());
  }
};
QTEST_MAIN(EditorWallpaperTest)
#include "EditorWallpaperTest.moc"
