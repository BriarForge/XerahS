#include "PipelineResultDialog.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>
namespace xerahs::app {
QDialog *makePipelineResultDialog(const QJsonObject &report, QWidget *parent, const QStringList &selected, const QString &unknownAction) {
  auto *dialog = new QDialog(parent);
  dialog->setObjectName(QStringLiteral("retainedCaptureResultDialog"));
  dialog->setWindowTitle(QStringLiteral("Capture result")); dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->resize(680, 420);
  auto *layout = new QVBoxLayout(dialog);
  const auto state = report.value("pipeline_state").toString();
  auto *summary = new QLabel(state == "interrupted" ?
      QStringLiteral("Pipeline: interrupted. An action with an unknown outcome may have completed. Check its output before retrying; recovery does not repeat actions.") :
      QStringLiteral("Pipeline: %1. Completed describes a finished run; individual actions can still fail.").arg(state), dialog);
  summary->setTextFormat(Qt::PlainText); summary->setWordWrap(true); layout->addWidget(summary);
  const auto retry = report.value("extensions").toObject().value("com.xerahs.app").toObject().value("retry").toObject();
  if (!retry.isEmpty()) {
    QStringList preserved;
    for (const auto &id : retry.value("preserved_successful_actions").toArray()) preserved.append(id.toString());
    auto *lineage = new QLabel(QStringLiteral("Linked retry %1. Earlier successful actions kept: %2. Saved-file facts may refer to an earlier attempt.")
        .arg(retry.value("attempt").toInt()).arg(preserved.isEmpty() ? QStringLiteral("none") : preserved.join(QStringLiteral(", "))), dialog);
    lineage->setObjectName(QStringLiteral("captureRetryLineage")); lineage->setTextFormat(Qt::PlainText); lineage->setWordWrap(true); layout->addWidget(lineage);
  }
  auto *table = new QTreeWidget(dialog); table->setObjectName(QStringLiteral("captureActionResults"));
  table->setAccessibleName(QStringLiteral("Capture action outcomes")); table->setRootIsDecorated(false);
  table->setHeaderLabels({QStringLiteral("Action"), QStringLiteral("State"), QStringLiteral("Diagnostic"), QStringLiteral("Input artifact")});
  QSet<QString> known;
  for (const auto &value : report.value("ordered_results").toArray()) {
    const auto action = value.toObject(); known.insert(action.value("action").toString());
    new QTreeWidgetItem(table, {action.value("action").toString(), action.value("state").toString(),
        action.value("diagnostic").toString(), action.value("input_artifact").toString()});
  }
  for (const auto &id : selected) if (!known.contains(id))
    new QTreeWidgetItem(table, {id, id == unknownAction ? QStringLiteral("Outcome unknown") : QStringLiteral("Not started"), {}, {}});
  for (int column = 0; column < 4; ++column) table->resizeColumnToContents(column);
  layout->addWidget(table);
  auto *privacy = new QLabel(QStringLiteral("Report details contain private saved-file paths and naming settings. Image pixels, clipboard contents and recognized text are excluded. "
                                           "Reviewing does not retry actions, modify files or change the clipboard."), dialog);
  privacy->setTextFormat(Qt::PlainText); privacy->setWordWrap(true); layout->addWidget(privacy);
  auto *details = new QPlainTextEdit(dialog); details->setObjectName(QStringLiteral("captureResultJson"));
  details->setAccessibleName(QStringLiteral("Capture result report JSON")); details->setReadOnly(true);
  details->setPlainText(QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented))); details->hide();
  auto *toggle = new QPushButton(QStringLiteral("Show report details"), dialog); toggle->setCheckable(true);
  QObject::connect(toggle, &QPushButton::toggled, details, &QWidget::setVisible);
  QObject::connect(toggle, &QPushButton::toggled, toggle, [toggle](bool visible) { toggle->setText(visible ? QStringLiteral("Hide report details") : QStringLiteral("Show report details")); });
  layout->addWidget(toggle); layout->addWidget(details);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog); layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  table->setFocus(); return dialog;
}
} // namespace xerahs::app
