#include "uploads/CredentialStore.h"
#include "uploads/UploadDestination.h"

#include <QJsonDocument>
#include <QTest>

using namespace xerahs::uploads;

namespace {

UploadDestination sample() {
  UploadDestination destination;
  destination.id = QUuid(QStringLiteral("12345678-1234-1234-1234-123456789abc"));
  destination.name = QStringLiteral("Example");
  destination.method = RequestMethod::Put;
  destination.requestUrl = QStringLiteral("https://example.com/upload");
  destination.query = {{QStringLiteral("album"), QStringLiteral("shots")}};
  destination.headers = {{QStringLiteral("Accept"), QStringLiteral("application/json")}};
  destination.body = BodyType::MultipartFormData;
  destination.arguments = {{QStringLiteral("title"), QStringLiteral("shot")}};
  destination.fileFormName = QStringLiteral("file");
  destination.resultUrl = QStringLiteral("{json:url}");
  destination.thumbnailUrl = QStringLiteral("{json:thumb}");
  destination.deletionUrl = QStringLiteral("{json:delete}");
  destination.errorMessage = QStringLiteral("{json:error}");
  destination.extra = QJsonObject{{QStringLiteral("vendorNote"), QStringLiteral("keep-me")},
                                  {QStringLiteral("nested"), QJsonObject{{QStringLiteral("a"), 1}}}};
  return destination;
}

}  // namespace

class UploadDestinationTest : public QObject {
  Q_OBJECT

private slots:
  void roundTripRetainsUnknownFields() {
    const auto destination = sample();
    const auto bytes = writeDestination(destination);
    const auto read = readDestination(bytes);
    QVERIFY(read.code.isEmpty());
    QVERIFY(read.destination.has_value());
    const auto &parsed = *read.destination;
    QCOMPARE(parsed.id, destination.id);
    QCOMPARE(parsed.name, destination.name);
    QCOMPARE(parsed.schemaVersion, 1);
    QCOMPARE(parsed.method, RequestMethod::Put);
    QCOMPARE(parsed.requestUrl, destination.requestUrl);
    QCOMPARE(parsed.query, destination.query);
    QCOMPARE(parsed.headers, destination.headers);
    QCOMPARE(parsed.body, BodyType::MultipartFormData);
    QCOMPARE(parsed.arguments, destination.arguments);
    QCOMPARE(parsed.fileFormName, destination.fileFormName);
    QCOMPARE(parsed.resultUrl, destination.resultUrl);
    QCOMPARE(parsed.thumbnailUrl, destination.thumbnailUrl);
    QCOMPARE(parsed.deletionUrl, destination.deletionUrl);
    QCOMPARE(parsed.errorMessage, destination.errorMessage);
    QCOMPARE(parsed.extra.value(QStringLiteral("vendorNote")).toString(), QStringLiteral("keep-me"));
    QCOMPARE(parsed.extra.value(QStringLiteral("nested")).toObject().value(QStringLiteral("a")).toInt(), 1);
    QVERIFY(!parsed.extra.contains(QStringLiteral("url")));
    QCOMPARE(QJsonDocument::fromJson(writeDestination(parsed)).object(), QJsonDocument::fromJson(bytes).object());
    QVERIFY(validateDestination(parsed).isEmpty());
  }

  void validationErrorsAreStable() {
    UploadDestination destination;
    destination.method = RequestMethod::Unknown;
    destination.rawMethod = QStringLiteral("TRACE");
    destination.body = BodyType::MultipartFormData;
    QCOMPARE(validateDestination(destination),
             QStringList({QStringLiteral("empty-id"), QStringLiteral("empty-name"), QStringLiteral("unsupported-method"),
                          QStringLiteral("empty-url"), QStringLiteral("missing-file-form-name")}));

    destination = sample();
    destination.requestUrl = QStringLiteral("file:///tmp/shot.png");
    QCOMPARE(validateDestination(destination), QStringList{QStringLiteral("non-http-scheme")});
    destination.requestUrl = QStringLiteral("ftp://example.com/shot");
    QCOMPARE(validateDestination(destination), QStringList{QStringLiteral("non-http-scheme")});
    destination.requestUrl = QStringLiteral("example.com/shot");
    QCOMPARE(validateDestination(destination), QStringList{QStringLiteral("invalid-url")});
    destination.requestUrl = QStringLiteral("https://user:secret@example.com/shot");
    QCOMPARE(validateDestination(destination), QStringList{QStringLiteral("url-userinfo")});
    destination.requestUrl = QStringLiteral("http://example.com/shot");
    QVERIFY(validateDestination(destination).isEmpty());
    destination.body = BodyType::Unknown;
    destination.rawBody = QStringLiteral("XML");
    QCOMPARE(validateDestination(destination), QStringList{QStringLiteral("unsupported-body")});
  }

  void unsupportedMethodRoundTrips() {
    const auto read = readDestination(QByteArray(
        "{\"schemaVersion\":1,\"id\":\"12345678-1234-1234-1234-123456789abc\",\"name\":\"N\",\"method\":\"TRACE\","
        "\"url\":\"https://example.com/up\",\"body\":\"None\"}"));
    QVERIFY(read.destination.has_value());
    QCOMPARE(read.destination->method, RequestMethod::Unknown);
    QCOMPARE(read.destination->rawMethod, QStringLiteral("TRACE"));
    QCOMPARE(validateDestination(*read.destination), QStringList{QStringLiteral("unsupported-method")});
    const auto again = readDestination(writeDestination(*read.destination));
    QVERIFY(again.destination.has_value());
    QCOMPARE(again.destination->rawMethod, QStringLiteral("TRACE"));
  }

  void rejectsBrokenDocuments() {
    QCOMPARE(readDestination(QByteArray("{")).code, QStringLiteral("invalid-json"));
    QCOMPARE(readDestination(QByteArray(
                 "{\"schemaVersion\":2,\"id\":\"12345678-1234-1234-1234-123456789abc\",\"name\":\"N\",\"method\":\"POST\","
                 "\"url\":\"https://example.com\",\"body\":\"None\"}"))
                 .code,
             QStringLiteral("unsupported-schema"));
    QCOMPARE(readDestination(QByteArray(kMaxDestinationBytes + 1, 'x')).code, QStringLiteral("too-large"));
  }

  void memoryCredentialStoreResults() {
    MemoryCredentialStore store;
    const auto id = QUuid(QStringLiteral("12345678-1234-1234-1234-123456789abc"));
    const auto other = QUuid(QStringLiteral("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"));
    QCOMPARE(store.store(id, QStringLiteral("Authorization"), QStringLiteral("secret-value")).status, CredentialStatus::Ok);
    QCOMPARE(store.store(id, QStringLiteral("api_key"), QStringLiteral("other-secret")).status, CredentialStatus::Ok);
    QCOMPARE(store.store(other, QStringLiteral("Authorization"), QStringLiteral("elsewhere")).status, CredentialStatus::Ok);

    const auto found = store.lookup(id, QStringLiteral("Authorization"));
    QCOMPARE(found.status, CredentialStatus::Ok);
    QCOMPARE(found.value, QStringLiteral("secret-value"));
    QCOMPARE(store.lookup(id, QStringLiteral("missing")).status, CredentialStatus::NotFound);
    QCOMPARE(store.lookup(id, QStringLiteral("missing")).value, QString());
    QCOMPARE(store.remove(id, QStringLiteral("missing")).status, CredentialStatus::NotFound);
    QCOMPARE(store.remove(id, QStringLiteral("api_key")).status, CredentialStatus::Ok);
    QCOMPARE(store.lookup(id, QStringLiteral("api_key")).status, CredentialStatus::NotFound);
    QCOMPARE(store.lookup(id, QStringLiteral("Authorization")).value, QStringLiteral("secret-value"));

    QCOMPARE(store.removeAll(id).status, CredentialStatus::Ok);
    QCOMPARE(store.lookup(id, QStringLiteral("Authorization")).status, CredentialStatus::NotFound);
    QCOMPARE(store.lookup(other, QStringLiteral("Authorization")).value, QStringLiteral("elsewhere"));
    QCOMPARE(store.removeAll(id).status, CredentialStatus::Ok);

    QCOMPARE(store.store(id, QString(), QStringLiteral("x")).status, CredentialStatus::Denied);
    QCOMPARE(store.lookup({}, QStringLiteral("Authorization")).status, CredentialStatus::Denied);
    QCOMPARE(store.removeAll({}).status, CredentialStatus::Denied);

    store.setForcedStatus(CredentialStatus::Unavailable);
    QCOMPARE(store.store(id, QStringLiteral("Authorization"), QStringLiteral("new-secret")).status, CredentialStatus::Unavailable);
    QCOMPARE(store.lookup(id, QStringLiteral("Authorization")).status, CredentialStatus::Unavailable);
    store.setForcedStatus(CredentialStatus::Denied);
    QCOMPARE(store.remove(other, QStringLiteral("Authorization")).status, CredentialStatus::Denied);
    QCOMPARE(store.removeAll(other).status, CredentialStatus::Denied);
    store.setForcedStatus(std::nullopt);
    QCOMPARE(store.lookup(other, QStringLiteral("Authorization")).value, QStringLiteral("elsewhere"));
  }
};

QTEST_GUILESS_MAIN(UploadDestinationTest)
#include "UploadDestinationTest.moc"
