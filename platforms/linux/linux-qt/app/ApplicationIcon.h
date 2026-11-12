// CORE-028: original XerahS application mark (an X inside a capture frame).
// Pixmaps are drawn in process. Call after QGuiApplication exists.
// No file, network, or Qt Svg dependency. The installed scalable icon is
// packaging/icons/com.xerahs.app.svg and uses this same geometry.
#pragma once

#include <QIcon>

namespace xerahs::app {

QIcon applicationIcon();

}  // namespace xerahs::app
