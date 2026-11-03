#pragma once
class QWidget;
namespace xerahs::app {
// Register native editors for the application's Quit event. A refused Close
// keeps Quit pending; no preferences may bypass an editor's close policy.
void guardEditorShutdown(QWidget &editor);
}
