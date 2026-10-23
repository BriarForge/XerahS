// CORE-007, EU-001: one dialog instance per tool. A second trigger raises it.
#include "features/ToolsFeature.h"

#include "AppContext.h"
#include "ColorPickerDialog.h"
#include "HashCheckerDialog.h"
#include "IconConverterDialog.h"
#include "VideoConverterDialog.h"

#ifdef XERAHS_VIDEO_CONVERTER
#include "VideoConverter.h"
#endif

#include <QDialog>

#include <memory>

namespace xerahs::app {
namespace {

CommandSurfaces toolSurfaces(bool fileMenu) {
  auto surfaces = CommandSurface::Tray | CommandSurface::Window | CommandSurface::ToolsMenu;
  if (fileMenu) surfaces |= CommandSurface::FileMenu;
  return surfaces;
}

template <typename Dialog>
QAction *addDialog(CommandRegistry &commands, const CommandSpec &spec, const std::function<Dialog *()> &create,
    const std::function<void(Dialog *)> &shown = {}) {
  auto *action = commands.add(spec);
  if (!action) return nullptr;
  auto slot = std::make_shared<QPointer<Dialog>>();
  QObject::connect(action, &QAction::triggered, action, [slot, create, shown] {
    presentOwnedDialog(*slot, create);
    if (*slot && shown) shown(slot->data());
  });
  return action;
}

QWidget *parentWidget(const std::function<QWidget *()> &parent) { return parent ? parent() : nullptr; }

}  // namespace

void installToolDialogs(CommandRegistry &commands, const std::function<QWidget *()> &parent) {
  addDialog<ColorPickerDialog>(commands,
      CommandSpec{QStringLiteral("pick-colour"), QStringLiteral("Pick colour…"), CommandGroup::Tools, toolSurfaces(true)},
      [parent] { return new ColorPickerDialog(parentWidget(parent)); },
      [](ColorPickerDialog *dialog) { dialog->pick(); });
  addDialog<HashCheckerDialog>(commands,
      CommandSpec{QStringLiteral("hash-checker"), QStringLiteral("Hash checker…"), CommandGroup::Tools, toolSurfaces(false)},
      [parent] { return new HashCheckerDialog(parentWidget(parent)); });
  addDialog<IconConverterDialog>(commands,
      CommandSpec{QStringLiteral("icon-converter"), QStringLiteral("Icon converter…"), CommandGroup::Tools, toolSurfaces(false)},
      [parent] { return new IconConverterDialog(parentWidget(parent)); });
#ifdef XERAHS_VIDEO_CONVERTER
  static GstElementAvailability availability;
  addDialog<VideoConverterDialog>(commands,
      CommandSpec{QStringLiteral("video-converter"), QStringLiteral("Video converter…"), CommandGroup::Tools, toolSurfaces(false)},
      [parent] {
        return new VideoConverterDialog(new GstConversionRunner, &availability, parentWidget(parent));
      });
#endif
}

void installTools(AppContext &ctx) { installToolDialogs(ctx.commands, ctx.dialogParent); }

}  // namespace xerahs::app
