// XerahS for Linux (linux-qt): tray application with region capture.
// Identity com.xerahs.app / "XerahS" (CORE-002, ROOT-IDENTITY-001).

#include "CaptureController.h"
#include "EditorWindow.h"
#include "EditorOpen.h"
#include "RecentImageMenu.h"
#include "NativeDesktopAppearance.h"
#include "AppearanceDialog.h"
#include "NativeInstance.h"
#include "ApplicationWindow.h"
#include "shell/ActivationDispatcher.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QUrl>

#include <cstdio>
#include <algorithm>
#include <tuple>

using xerahs::app::CaptureController;

namespace {
QString activationFailureMessage(const QString &diagnostic) {
  if (diagnostic == u"activation-busy") return QStringLiteral("XerahS is busy. Try again after the current capture or dialog finishes.");
  if (diagnostic == u"instance-unavailable") return QStringLiteral("The running XerahS instance is not ready. Try again after startup finishes.");
  if (diagnostic == u"activation-delivery-unknown") return QStringLiteral("XerahS did not acknowledge the request. Check the running instance before retrying.");
  if (diagnostic == u"instance-storage-denied") return QStringLiteral("XerahS could not secure its runtime files. Check the ownership and permissions of XDG_RUNTIME_DIR.");
  if (diagnostic == u"instance-endpoint-conflict") return QStringLiteral("The XerahS runtime endpoint is in use or unsafe. Close conflicting instances and check runtime permissions.");
  if (diagnostic == u"activation-peer-denied") return QStringLiteral("XerahS could not authenticate the running instance. Check your per-user runtime environment.");
  if (diagnostic == u"activation-invalid" || diagnostic == u"activation-version-unsupported") return QStringLiteral("The activation request is invalid or incompatible. Check the command and installed XerahS version.");
  return QStringLiteral("XerahS could not accept the activation. Check the running instance and try again.");
}

// Monochrome capture-frame mark, drawn so no asset pipeline is needed yet.
QIcon appIcon() {
  QIcon icon;
  for (int size : {16, 22, 24, 32, 48, 64, 128, 256}) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    const double s = size;
    QPainterPath body;
    body.addRoundedRect(QRectF(s * 0.06, s * 0.06, s * 0.88, s * 0.88), s * 0.2, s * 0.2);
    p.fillPath(body, QColor(24, 24, 27));
    QPen pen(QColor(245, 245, 245), std::max(1.5, s * 0.085), Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    p.setPen(pen);
    const double a = s * 0.27;
    const double b = s * 0.73;
    const double l = s * 0.15;
    for (const auto &[x, y, sx, sy] : {std::tuple{a, a, 1.0, 1.0}, std::tuple{b, a, -1.0, 1.0},
                                       std::tuple{a, b, 1.0, -1.0}, std::tuple{b, b, -1.0, -1.0}}) {
      p.drawLine(QPointF(x, y), QPointF(x + sx * l, y));
      p.drawLine(QPointF(x, y), QPointF(x, y + sy * l));
    }
    p.end();
    icon.addPixmap(pixmap);
  }
  return icon;
}

}  // namespace

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("XerahS"));
  QApplication::setApplicationDisplayName(QStringLiteral("XerahS"));
  QApplication::setDesktopFileName(QStringLiteral("com.xerahs.app"));
  QApplication::setOrganizationDomain(QStringLiteral("xerahs.com"));
  QApplication::setQuitOnLastWindowClosed(false);
  QApplication::setWindowIcon(appIcon());

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("XerahS screen capture"));
  parser.addHelpOption();
  const QCommandLineOption captureRegion(QStringLiteral("capture-region"),
                                         QStringLiteral("Start a region capture (in the running instance if any)."));
  parser.addOption(captureRegion);
  const QCommandLineOption edit(QStringLiteral("edit"), QStringLiteral("Open an image in the editor."),
                                QStringLiteral("file"));
  parser.addOption(edit);
  parser.process(app);
  using namespace xerahs::shell;
  const auto request = makeActivationRequest(parser.isSet(edit) ? ActivationAction::EditImage :
      parser.isSet(captureRegion) ? ActivationAction::CaptureRegion : ActivationAction::Activate,
      parser.isSet(edit) ? xerahs::app::absoluteImagePath(parser.value(edit)) : QString{});
  xerahs::app::NativeInstance instance(xerahs::app::NativeInstance::runtimeDirectory());
  const auto started = instance.start(request);
  if (started.kind == xerahs::app::InstanceStart::Kind::Forwarded) return 0;
  if (started.kind != xerahs::app::InstanceStart::Kind::Primary) {
    std::fprintf(stderr, "xerahs: %s: %s\n", qPrintable(started.diagnostic), qPrintable(activationFailureMessage(started.diagnostic)));
    return started.kind == xerahs::app::InstanceStart::Kind::Busy ? 2 : 1;
  }

  // Initialize only in the winning native instance, before any editor opens.
  // The Avalonia configuration is never imported implicitly.
  const auto preferenceDiagnostics = xerahs::app::initializeNativeEditorPreferences(xerahs::app::nativeEditorPreferencePath());
  xerahs::app::nativeDesktopAppearance().start();
  if (!preferenceDiagnostics.isEmpty()) {
    QStringList messages;
    for (const auto &code : preferenceDiagnostics) messages.append(xerahs::app::editorPreferenceDiagnosticMessage(code));
    QMessageBox::warning(nullptr, QStringLiteral("Editor preferences"), messages.join(QLatin1Char('\n')));
  }
  // Capture destination preferences remain Required; the current destination
  // is the XDG pictures directory confirmed by the product owner.
  const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  CaptureController capture(QDir(pictures.isEmpty() ? QDir::homePath() : pictures).filePath(QStringLiteral("XerahS")));

  QSystemTrayIcon tray(appIcon());
  tray.setToolTip(QStringLiteral("XerahS"));
  QMenu menu;
  QAction *region = menu.addAction(QStringLiteral("Capture region"));
  QAction *cancelCapture = menu.addAction(QStringLiteral("Cancel capture"));
  cancelCapture->setEnabled(false);
  QObject::connect(cancelCapture, &QAction::triggered, &capture, &CaptureController::cancel);
  QAction *newImage = menu.addAction(QStringLiteral("New image…"));
  QAction *openImage = menu.addAction(QStringLiteral("Open image…"));
  xerahs::app::addRecentImagesMenu(&menu, [](const QString &path) {
    if (auto *window = xerahs::app::EditorWindow::open(path)) window->show();
  }, [] { xerahs::app::manageRecentImages(); });
  QAction *folder = menu.addAction(QStringLiteral("Open captures folder"));
  QAction *appearance = menu.addAction(QStringLiteral("Editor appearance…"));
  menu.addSeparator();
  QAction *quit = menu.addAction(QStringLiteral("Quit XerahS"));
  tray.setContextMenu(&menu);
  xerahs::app::ApplicationWindow home({{"capture-region", region}, {"cancel-capture", cancelCapture},
      {"new-image", newImage}, {"open-image", openImage}, {"captures-folder", folder}, {"editor-appearance", appearance}, {"quit", quit}});
  auto *showHome = new QAction(QStringLiteral("Open XerahS"), &menu); menu.insertAction(region, showHome);
  QObject::connect(showHome, &QAction::triggered, &home, &xerahs::app::ApplicationWindow::activate);

  QObject::connect(newImage, &QAction::triggered, [] {
    if (auto *window = xerahs::app::EditorWindow::createNew()) window->show();
  });
  QObject::connect(folder, &QAction::triggered, [&capture] {
    QDir().mkpath(capture.saveDirectory());
    QDesktopServices::openUrl(QUrl::fromLocalFile(capture.saveDirectory()));
  });
  QObject::connect(quit, &QAction::triggered, &app, &QApplication::quit);
  QObject::connect(appearance, &QAction::triggered, [] { xerahs::app::manageEditorAppearance(); });
  // CORE-007: availability is shown, not faked.
  QObject::connect(&capture, &CaptureController::busyChanged, region, [region, cancelCapture, &capture](bool) {
    const bool busy = capture.busy();
    region->setEnabled(!busy);
    cancelCapture->setEnabled(busy);
    region->setText(busy ? QStringLiteral("Capture region (in progress)") : QStringLiteral("Capture region"));
  });
  QObject::connect(&capture, &CaptureController::notify, &tray,
                   [&tray](const QString &title, const QString &body, bool success) {
                     tray.showMessage(title, body, success ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning,
                                      5000);
                   });
  QObject::connect(&capture, &CaptureController::notify, &home, &xerahs::app::ApplicationWindow::showResult);
  ActivationDispatcher dispatcher({[&home] { home.activate(); }, [&capture] { capture.captureRegion(); },
      [](const QString &path) { if (auto *window = xerahs::app::EditorWindow::open(path)) window->show(); },
      [&capture] { return capture.busy(); }, [] { return QApplication::activeModalWidget() != nullptr; }});
  instance.setAdmission([&dispatcher](const ActivationRequest &incoming) { return dispatcher.admit(incoming); });
  const auto dispatch = [&dispatcher, &home](const ActivationRequest &incoming) {
    const auto diagnostic = dispatcher.dispatch(incoming);
    if (!diagnostic.isEmpty()) { home.showResult(QStringLiteral("Activation"), activationFailureMessage(diagnostic), false); home.activate(); }
  };
  QObject::connect(&instance, &xerahs::app::NativeInstance::activationRequested, &home, dispatch);
  const auto invokeLocal = [&dispatcher, &dispatch, &home](ActivationAction action, const QString &path = QString{}) {
    const auto local = makeActivationRequest(action, path);
    const auto admitted = dispatcher.admit(local);
    if (admitted == ActivationReplyState::Accepted) dispatch(local);
    else {
      home.showResult(QStringLiteral("Activation"), activationFailureMessage(admitted == ActivationReplyState::Busy ? QStringLiteral("activation-busy") : QStringLiteral("activation-unavailable")), false);
      home.activate();
    }
  };
  QObject::connect(region, &QAction::triggered, &home, [&invokeLocal] { invokeLocal(ActivationAction::CaptureRegion); });
  QObject::connect(openImage, &QAction::triggered, &home, [&invokeLocal, &capture, &home] {
    const auto path = QFileDialog::getOpenFileName(&home, QStringLiteral("Open image"), capture.saveDirectory(), xerahs::app::imageFileFilter());
    if (!path.isEmpty()) invokeLocal(ActivationAction::EditImage, path);
  });
  QObject::connect(&tray, &QSystemTrayIcon::activated, region, [region](QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger) region->trigger();
  });

  const bool trayAvailable = QSystemTrayIcon::isSystemTrayAvailable();
  if (!trayAvailable) {
    std::fprintf(stderr, "xerahs: no system tray host; use the XerahS window\n");
    home.show();
  }
  app.setQuitOnLastWindowClosed(!trayAvailable);
  tray.show();
  if (dispatcher.admit(request) == ActivationReplyState::Accepted) dispatch(request);
  const int result = app.exec(); instance.setAdmission({}); return result;
}
