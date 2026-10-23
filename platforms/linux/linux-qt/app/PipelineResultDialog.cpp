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
  const auto native = report.value("extensions").toObject().value("com.xerahs.app").toObject();
  const auto upload = native.value("upload_result").toObject();
  for (const auto *key : {"url", "thumbnail_url", "deletion_url", "error_message"}) {
    const auto text = upload.value(QLatin1String(key)).toString(); if (text.isEmpty()) continue;
    const QString title = QByteArray(key) == "url" ? "URL" : QByteArray(key) == "thumbnail_url" ? "Thumbnail URL" :
        QByteArray(key) == "deletion_url" ? "Deletion URL" : "Upload error";
    auto *line = new QLabel(title + ": " + text, dialog);
    line->setObjectName(QStringLiteral("captureUpload_") + QLatin1String(key));
    line->setTextFormat(Qt::PlainText); line->setWordWrap(true); line->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(line);
  }
  auto artifacts = native.value("artifact_outcomes").toArray(); QSet<QString> acknowledged;
  for (const auto &entry : artifacts) acknowledged.insert(entry.toObject().value("artifact").toString());
  for (const auto &entry : native.value("file_artifacts").toArray()) {
    const auto file = entry.toObject(); const auto artifact = file.value("artifact").toString();
    if (!acknowledged.contains(artifact)) {
      artifacts.append(QJsonObject{{"action", artifact.section('.', 0, 0)}, {"artifact", artifact}, {"state", "publication acknowledged"}});
      acknowledged.insert(artifact);
    }
  }
  const auto unknownArtifact = native.value("recovery").toObject().value("unknown_artifact").toString();
  if (!unknownArtifact.isEmpty()) artifacts.append(QJsonObject{{"action", unknownArtifact.section('.', 0, 0)}, {"artifact", unknownArtifact}, {"state", "Outcome unknown"}});
  if (!artifacts.isEmpty()) {
    auto *files = new QTreeWidget(dialog); files->setObjectName("captureArtifactResults"); files->setRootIsDecorated(false);
    files->setAccessibleName("Capture artifact outcomes"); files->setHeaderLabels({"Action", "Artifact", "State", "Diagnostic"});
    for (const auto &value : artifacts) {
      const auto fact = value.toObject();
      const auto id = fact.value("artifact").toString(); const auto action = fact.value("action").toString();
      const auto artifactLabel = id == "save.file" || id == "save-as.file" ? QString("PNG file") :
          id == "save.sidecar" || id == "save-as.sidecar" ? QString("Editable annotations (.xann)") : id;
      auto *item = new QTreeWidgetItem(files, {action == "save" ? QString("Save") : action == "save-as" ? QString("Save As") : action, artifactLabel,
          fact.value("absent").toBool() ? "succeeded (no companion needed)" : fact.value("state").toString(), fact.value("diagnostic").toString()});
      item->setData(1, Qt::UserRole, id);
    }
    for (int column = 0; column < 4; ++column) files->resizeColumnToContents(column);
    layout->addWidget(files);
  }
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
