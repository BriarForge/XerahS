// Shared native tool controls for the editor and capture annotation surface.
#pragma once
#include <QMap>
#include <QString>
#include "image-editor/EditorSession.h"
#include <functional>
class QAction;
class QLabel;
class QObject;
class QWidget;
namespace xerahs::app {
class EditorCanvas;
QMap<QString, QAction *> annotationToolActions(EditorCanvas &, QObject *owner, bool includeCrop = true);
QMap<QString, QAction *> annotationOrderActions(EditorCanvas &, QObject *owner,
    std::function<void(xerahs::editor::EditorSession::Order)> dispatch = {}, std::function<bool()> ready = {});
QLabel *annotationToolInstructions(EditorCanvas &, QWidget *parent);
QString annotationEditMessage(const QString &diagnostic);
} // namespace xerahs::app
