#include "RecordingObservation.h"
#include "actions/PipelineResultJson.h"
#include <QDebug>
#include <QPointer>
namespace xerahs::app {
RecordingObservation::RecordingObservation(recording::Session &session, QObject *parent) : QObject(parent) {
  connect(&session, &recording::Session::finished, this, [this, &session] {
    const auto observation = session.terminalObservation();
    const auto text = xerahs::actions::pipelineObservationText(observation);
    qInfo().noquote() << "Workflow terminal" << text;
    QPointer<RecordingObservation> self(this);
    emit activityChanged(text);
    if (!self) return;
    emit terminalObserved(observation);
    if (!self) return;
    const bool success = observation.value("state") == "succeeded";
    emit notify(success ? QString("Screen recording completed") : QString("Screen recording needs review"), text, success);
  });
}
}
