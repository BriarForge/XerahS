#pragma once
#include <QString>
class QWidget;
namespace xerahs::app {
// Register native editors for the application's Quit event. A refused Close
// keeps Quit pending; no preferences may bypass an editor's close policy.
void guardEditorShutdown(QWidget &editor);
// Empty only when every registered editor accepted its native close policy.
QString closeEditorsForShutdown();
// Call after QApplication::exec returns, when Qt permits nested event loops again.
void drainEditorRecovery();
// Session-manager seam: without interaction permission leave documents intact;
// the caller must cancel shutdown on a nonempty result. No implicit save.
QString commitEditorsForSession(bool interactionAllowed);
}
