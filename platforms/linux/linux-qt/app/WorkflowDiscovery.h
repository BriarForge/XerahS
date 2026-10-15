#pragma once
#include <QJsonObject>
namespace xerahs::app {
// Read-only private saved-definition query. Does not contact a running instance,
// initialize preferences, connect a desktop service or create a GUI.
QJsonObject discoverNativeWorkflows();
} // namespace xerahs::app
