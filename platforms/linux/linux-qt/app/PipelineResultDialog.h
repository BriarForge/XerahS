#pragma once
#include <QJsonObject>
#include <QStringList>
class QDialog;
class QWidget;
namespace xerahs::app {
QDialog *makePipelineResultDialog(const QJsonObject &report, QWidget *parent, const QStringList &selected = {},
                                  const QString &unknownAction = {});
} // namespace xerahs::app
