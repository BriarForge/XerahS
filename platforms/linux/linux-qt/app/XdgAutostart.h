#pragma once
#include <QString>
namespace xerahs::app {
// Empty configHome uses XDG_CONFIG_HOME, then ~/.config. No shell is involved.
QString setAutostart(bool enabled, const QString &executable, const QString &configHome = {});
}
