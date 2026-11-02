#include "EditorWallpaper.h"
#include "ComparisonImages.h"
#include "EditorPreparation.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QColorSpace>
#include <QComboBox>
#include <QDateTime>
#include <QDBusConnection>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QVBoxLayout>
#include <memory>

namespace xerahs::app {
using namespace xerahs::actions;
namespace {
ActionOutcome failed(const QString &code) { return {ActionState::Failed, code, {}}; }
ActionOutcome cancelled() { return {ActionState::Cancelled, {}, {}}; }
EditorWallpaperOutcome only(const ActionOutcome &action) { EditorWallpaperOutcome outcome; outcome.action = action; return outcome; }
bool stopping(const xerahs::editor::RenderControl &control) { return control.cancelled && control.cancelled(); }

// Atomic owner-only publication: never a partial PNG at the final path.
QString writePrivatePng(const QString &directory, const QString &path, const QByteArray &png,
                        const xerahs::editor::RenderControl &control) {
  if (stopping(control)) return {};
  QDir dir(directory);
  if (!dir.exists() && !dir.mkpath(".")) return "wallpaper-storage-failed";
  const QFileInfo info(directory);
  if (info.isSymLink() || !info.isDir()) return "wallpaper-storage-failed";
  if (!QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) return "wallpaper-storage-failed";
  if (QFileInfo::exists(path) || QFileInfo(path).isSymLink()) return "wallpaper-storage-failed";
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) return "wallpaper-storage-failed";
  if (file.write(png) != png.size() || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) { file.cancelWriting(); return "wallpaper-storage-failed"; }
  if (stopping(control)) { file.cancelWriting(); return {}; }
  return file.commit() ? QString() : QStringLiteral("wallpaper-storage-failed");
}

void destroyProgress(QPointer<QProgressDialog> dialog, QObject *context) {
  if (dialog) QObject::disconnect(dialog, nullptr, context, nullptr);
  if (dialog) dialog->hide();
  if (dialog) dialog->setParent(nullptr);  // a child-destruction observer may delete the parent
  delete dialog.data();
}

struct PortalRun { PortalWallpaper::Outcome outcome = PortalWallpaper::Outcome::Failed; bool abandoned = false; };
PortalRun callPortal(QWidget *parent, const EditorWallpaperPorts &ports, const QString &path, const EditorWallpaperChoice &choice) {
  PortalRun run; const QPointer<QWidget> owner(parent); QEventLoop loop; bool finished = false;
  auto *portal = new PortalWallpaper(QDBusConnection::sessionBus(), ports.portal);
  QPointer<QProgressDialog> dialog;
  const auto abandon = [&] { run.abandoned = !finished; portal->cancel(); loop.quit(); };
  QObject::connect(portal, &PortalWallpaper::finished, &loop, [&](PortalWallpaper::Outcome outcome) {
    if (!finished) { finished = true; run.outcome = outcome; }
    loop.quit();
  });
  dialog = new QProgressDialog("Waiting for the desktop to apply the wallpaper…", "Cancel", 0, 0, parent);
  dialog->setObjectName("wallpaperApplyProgress"); dialog->setAccessibleName("Set as Wallpaper progress");
  dialog->setWindowTitle("Set as Wallpaper"); dialog->setWindowModality(Qt::WindowModal);
  dialog->setAutoClose(false); dialog->setAutoReset(false); dialog->setMinimumDuration(0);
  QObject::connect(dialog, &QProgressDialog::canceled, &loop, abandon);
  QObject::connect(dialog, &QDialog::rejected, &loop, abandon);
  QObject::connect(dialog, &QObject::destroyed, &loop, abandon);
  if (parent) QObject::connect(parent, &QObject::destroyed, &loop, abandon);
  dialog->open();
  portal->request(path, choice.target, choice.showPreview);
  if (!finished && !run.abandoned) loop.exec();
  QObject::disconnect(portal, nullptr, &loop, nullptr);
  destroyProgress(dialog, &loop);
  if (!finished) { run.abandoned = true; portal->cancel(); }  // closes the portal Request if one exists
  portal->retire();
  return run;
}

void removeUnusedFile(const QString &path) { if (!path.isEmpty()) QFile::remove(path); }
}

QString defaultWallpaperDirectory() {
  const auto base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  return base.isEmpty() ? QString{} : QDir(base).filePath("com.xerahs.app/linux-qt/wallpapers");
}

EditorWallpaperChoice chooseEditorWallpaper(const EditorWallpaperSnapshot &snapshot, QWidget *parent) {
  QPointer<QDialog> dialog(new QDialog(parent));
  dialog->setObjectName("confirmEditorWallpaper"); dialog->setWindowTitle("Set as Wallpaper"); dialog->setModal(true);
  dialog->setAccessibleName("Set as Wallpaper confirmation");
  auto *layout = new QVBoxLayout(dialog);
  auto *intro = new QLabel(QStringLiteral("Set the rendered image (%1 × %2) as your desktop wallpaper through the desktop portal?\n\n"
      "The PNG includes source pixels and visible supported annotations, converted to sRGB with alpha. "
      "Transparent areas are shown as the desktop chooses. The editable document stays open and unchanged.")
      .arg(snapshot.image.width()).arg(snapshot.image.height()), dialog);
  intro->setWordWrap(true); intro->setTextFormat(Qt::PlainText); layout->addWidget(intro);
  auto *form = new QFormLayout; layout->addLayout(form);
  auto *target = new QComboBox(dialog); target->setObjectName("wallpaperTarget"); target->setAccessibleName("Apply wallpaper to");
  target->addItem("Desktop background", int(PortalWallpaper::Target::Background));
  target->addItem("Lock screen", int(PortalWallpaper::Target::Lockscreen));
  target->addItem("Desktop background and lock screen", int(PortalWallpaper::Target::Both));
  form->addRow("Apply to", target);
  auto *preview = new QCheckBox("Let the desktop show a preview before applying", dialog);
  preview->setObjectName("wallpaperShowPreview"); preview->setChecked(true); form->addRow(preview);
  auto *displays = new QLabel("Display layout: the desktop portal has no per-display choice. The desktop applies this image to "
      "all displays, using its own fill or fit setting.", dialog);
  displays->setObjectName("wallpaperDisplayNote"); displays->setWordWrap(true); displays->setTextFormat(Qt::PlainText); layout->addWidget(displays);
  auto *file = new QLabel(QStringLiteral("The PNG will be kept at this private location (owner read/write only), so the desktop can keep using it:\n%1")
      .arg(snapshot.path), dialog);
  file->setObjectName("wallpaperFilePath"); file->setWordWrap(true); file->setTextFormat(Qt::PlainText);
  file->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(file);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  buttons->button(QDialogButtonBox::Ok)->setText("Set wallpaper"); buttons->button(QDialogButtonBox::Cancel)->setDefault(true);
  buttons->button(QDialogButtonBox::Cancel)->setFocus();
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  layout->addWidget(buttons);
  const bool accepted = dialog->exec() == QDialog::Accepted;
  if (!dialog) return {};  // deleted by an observer during exec: treat as declined
  EditorWallpaperChoice choice;
  if (accepted) {
    choice.action = ActionOutcome{}; choice.target = PortalWallpaper::Target(target->currentData().toInt());
    choice.showPreview = preview->isChecked();
  }
  delete dialog.data();
  return choice;
}

EditorWallpaperOutcome runEditorWallpaper(const QImage &source, const xerahs::editor::AnnotationDocument &document,
                                         QWidget *parent, const EditorWallpaperPorts &ports) {
  const auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
  if (!application || QThread::currentThread() != application->thread() || !ports.confirm) return only(failed("wallpaper-unavailable"));
  const QString directory = ports.directory.isEmpty() ? defaultWallpaperDirectory() : ports.directory;
  if (directory.isEmpty()) return only(failed("wallpaper-storage-failed"));
  const QPointer<QWidget> owner(parent);
  struct Prepared { EditorWallpaperSnapshot snapshot; QString diagnostic; };
  const auto prepared = std::make_shared<Prepared>();
  const auto ready = runEditorPreparation(parent,
      {"wallpaperImageProgress", "Set as Wallpaper", "Rendering source and visible annotations as sRGB with alpha…", "Set as Wallpaper render progress"},
      [source, document, prepared, beforeRender = ports.beforeRender](const xerahs::editor::RenderControl &control) {
        if (beforeRender) beforeRender();
        if (stopping(control)) return;
        if (!xerahs::editor::parseDocument(xerahs::editor::serializeDocument(document)).document ||
            source.width() != document.canvasWidth || source.height() != document.canvasHeight) { prepared->diagnostic = "wallpaper-render-failed"; return; }
        const auto rendered = prepareComparisonImage(source, document.annotations, control);
        if (rendered.image.isNull()) {
          prepared->diagnostic = rendered.diagnostic == "comparison-annotation-unsupported" ? "wallpaper-annotation-unsupported" : "wallpaper-render-failed"; return;
        }
        prepared->snapshot.image = rendered.image;
        if (!source.colorSpace().isValid()) prepared->snapshot.disclosures.append("wallpaper-untagged-assumed-srgb");
        else if (source.colorSpace() != QColorSpace(QColorSpace::SRgb)) prepared->snapshot.disclosures.append("wallpaper-colour-converted-to-srgb");
        QBuffer png(&prepared->snapshot.png);
        if (!png.open(QIODevice::WriteOnly) || !rendered.image.save(&png, "PNG")) prepared->diagnostic = "wallpaper-encode-failed";
      });
  if (ready == EditorPreparationResult::Cancelled || (parent && !owner)) return only(cancelled());
  if (ready != EditorPreparationResult::Ready || !prepared->diagnostic.isEmpty() || prepared->snapshot.png.isEmpty())
    return only(failed(prepared->diagnostic.isEmpty() ? QStringLiteral("wallpaper-render-failed") : prepared->diagnostic));
  // A fresh name per confirmation: the desktop may still be showing an earlier image.
  prepared->snapshot.path = QDir(directory).filePath(QStringLiteral("wallpaper-%1-%2.png")
      .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'"), QUuid::createUuid().toString(QUuid::Id128).left(8)));
  EditorWallpaperChoice choice;
  try { choice = ports.confirm(prepared->snapshot, parent); } catch (...) { return only(failed("wallpaper-confirmation-failed")); }
  if (parent && !owner) return only(cancelled());
  if (choice.action.outcomeUnknown || choice.action.replacementSelection || !choice.action.artifacts.empty()) return only(failed("wallpaper-confirmation-failed"));
  if (choice.action.state != ActionState::Succeeded) return only(choice.action.state == ActionState::Cancelled ? cancelled() : failed("wallpaper-confirmation-failed"));

  EditorWallpaperOutcome outcome; outcome.target = PortalWallpaper::targetName(choice.target);
  const auto stored = std::make_shared<QString>(); const auto path = prepared->snapshot.path;
  const auto write = runEditorPreparation(parent,
      {"wallpaperStoreProgress", "Set as Wallpaper", "Saving the wallpaper image…", "Set as Wallpaper save progress"},
      [stored, directory, path, png = prepared->snapshot.png](const xerahs::editor::RenderControl &control) {
        *stored = writePrivatePng(directory, path, png, control);
      });
  if (write == EditorPreparationResult::Cancelled || (parent && !owner)) { removeUnusedFile(path); outcome.action = cancelled(); return outcome; }
  if (write != EditorPreparationResult::Ready || !stored->isEmpty() || !QFileInfo::exists(path)) {
    removeUnusedFile(path); outcome.action = failed(stored->isEmpty() ? QStringLiteral("wallpaper-storage-failed") : *stored); return outcome;
  }
  outcome.filePath = path;

  const auto run = callPortal(parent, ports, path, choice);
  outcome.portalCalled = true;
  using Portal = PortalWallpaper::Outcome;
  if (run.abandoned) {
    // The request was closed, but the desktop may already have applied the image: keep the file.
    outcome.action = cancelled(); outcome.action.diagnostic = QStringLiteral("wallpaper-cancel-requested"); outcome.fileRetained = true; return outcome;
  }
  switch (run.outcome) {
    case Portal::Applied: outcome.action = {}; outcome.fileRetained = true; break;
    case Portal::Cancelled: outcome.action = cancelled(); break;
    case Portal::Denied: outcome.action = failed("wallpaper-permission-denied"); break;
    case Portal::Unavailable: outcome.action = failed("wallpaper-service-unavailable"); break;
    case Portal::Failed: outcome.action = failed("wallpaper-failed"); break;
  }
  if (!outcome.fileRetained) { removeUnusedFile(path); outcome.filePath.clear(); }
  return outcome;
}
} // namespace xerahs::app
