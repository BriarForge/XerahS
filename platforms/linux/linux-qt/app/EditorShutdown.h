#pragma once
#include <QString>
class QWidget;
namespace xerahs::app {
// Register native editors for the application's Quit event. A refused Close
// keeps Quit pending; no preferences may bypass an editor's close policy.
void guardEditorShutdown(QWidget &editor);
// Empty only when every registered editor accepted its native close policy.
QString closeEditorsForShutdown();
}
