// CORE-010: private previews keep private content out of desktop notifications.
#include "settings/ApplicationSettings.h"
#include "shell/NotificationPolicy.h"

#include <QTest>

using namespace xerahs::shell;

class NotificationPolicyTest : public QObject {
  Q_OBJECT
private slots:
  void defaultShowsTheFullText() {
    const auto text = presentNotification("Capture saved", "/home/mike/Pictures/bank-statement.png", true, false);
    QCOMPARE(text.title, QStringLiteral("Capture saved"));
    QCOMPARE(text.body, QStringLiteral("/home/mike/Pictures/bank-statement.png"));
  }

  void privatePreviewKeepsOperationAndTrueResultOnly_data() {
    QTest::addColumn<QString>("body");
    QTest::addColumn<bool>("ok");
    QTest::addColumn<QString>("expected");
    QTest::newRow("saved-path") << "/home/mike/Pictures/bank-statement.png" << true << "Completed. Open XerahS for details.";
    QTest::newRow("upload-url") << "https://example.test/u/SECRET-TOKEN-123" << true << "Completed. Open XerahS for details.";
    QTest::newRow("ocr-text") << "Recognised: account 12-3456 balance 99,999" << true << "Completed. Open XerahS for details.";
    QTest::newRow("coded-failure") << "upload-failed: https://example.test/u/SECRET-TOKEN-123 refused" << false
                                   << "upload-failed: attention required. Open XerahS for details.";
    QTest::newRow("uncoded-failure") << "Could not write /home/mike/secret/plan.png" << false << "Attention required. Open XerahS for details.";
    QTest::newRow("sentence-colon-is-not-a-code") << "Could not save: /home/mike/secret.png" << false << "Attention required. Open XerahS for details.";
  }
  void privatePreviewKeepsOperationAndTrueResultOnly() {
    QFETCH(QString, body); QFETCH(bool, ok); QFETCH(QString, expected);
    const auto text = presentNotification("Region capture", body, ok, true);
    QCOMPARE(text.title, QStringLiteral("Region capture"));
    QCOMPARE(text.body, expected);
    for (const auto &secret : {"/home/", "SECRET-TOKEN", "12-3456", "https://", "plan.png"}) QVERIFY2(!text.body.contains(secret), secret);
  }

  void settingIsOffByDefaultAndMalformedValuesDoNotEnableIt() {
    QVERIFY(!privatePreviewsEnabled({}));
    QVERIFY(!privatePreviewsEnabled({{kPrivateNotificationsKey, "true"}}));
    QVERIFY(!privatePreviewsEnabled({{kPrivateNotificationsKey, 1}}));
    QVERIFY(privatePreviewsEnabled({{kPrivateNotificationsKey, true}}));
    const auto defaults = xerahs::settings::defaultSettings("/tmp/captures");
    QVERIFY(defaults.contains(kPrivateNotificationsKey));
    QVERIFY(!privatePreviewsEnabled(defaults));
    bool known = false;
    for (const auto &field : xerahs::settings::settingFields()) known |= field.key == QLatin1String(kPrivateNotificationsKey);
    QVERIFY(known);
  }
};

QTEST_GUILESS_MAIN(NotificationPolicyTest)
#include "NotificationPolicyTest.moc"
