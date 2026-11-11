#include "LegacyHistory.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QXmlStreamReader>

namespace xerahs::history {
namespace {
LegacyHistoryPreview failed(const QString &code) {
  LegacyHistoryPreview result; result.format = LegacyHistoryFormat::Xml; result.code = code; return result;
}
bool xmlSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool unsafeName(const QString &qualified, const QString &local, const QString &uri = {}) {
  return compatibility::unsafeUnknown(qualified) || compatibility::unsafeUnknown(local) ||
      (local == "type" && uri == "http://www.w3.org/2001/XMLSchema-instance");
}
struct Reader {
  explicit Reader(const QByteArray &bytes) : xml(bytes) { xml.setEntityExpansionLimit(1); }
  QXmlStreamReader xml;
  qsizetype events = 0;
  int depth = -1; // the synthetic fragment container does not count as source depth
  QString code;
  bool next() {
    if (!code.isEmpty()) return false;
    const auto token = xml.readNext();
    if (++events > 100000) code = "legacy-history-xml-too-many-elements";
    else if (token == QXmlStreamReader::StartElement && ++depth > 64) code = "legacy-history-xml-too-deep";
    else if (token == QXmlStreamReader::EndElement) --depth;
    else if (token == QXmlStreamReader::DTD) code = "legacy-history-xml-dtd-forbidden";
    else if (token == QXmlStreamReader::EntityReference) code = "legacy-history-xml-entity-forbidden";
    else if (token == QXmlStreamReader::ProcessingInstruction) code = "legacy-history-xml-processing-instruction";
    if (code.isEmpty() && xml.hasError()) code = "legacy-history-xml-invalid";
    return code.isEmpty() && token != QXmlStreamReader::EndDocument;
  }
};
struct Node {
  QString local, qualified, text;
  QJsonObject raw;
  std::vector<Node> children;
  bool unsafe = false;
};
Node element(Reader &reader) {
  Node result; result.local = reader.xml.name().toString(); result.qualified = reader.xml.qualifiedName().toString();
  const auto uri = reader.xml.namespaceUri().toString();
  result.unsafe = unsafeName(result.qualified, result.local, uri);
  QJsonObject attributes, namespaces;
  for (const auto &attribute : reader.xml.attributes()) {
    const auto name = attribute.qualifiedName().toString(); attributes.insert(name, attribute.value().toString());
    result.unsafe |= unsafeName(name, attribute.name().toString(), attribute.namespaceUri().toString());
  }
  for (const auto &declaration : reader.xml.namespaceDeclarations()) namespaces.insert(declaration.prefix().toString(), declaration.namespaceUri().toString());
  QJsonArray content;
  while (reader.next()) {
    if (reader.xml.isEndElement()) break;
    if (reader.xml.isStartElement()) {
      auto child = element(reader); result.text += child.text; result.unsafe |= child.unsafe;
      content.append(QJsonObject{{child.qualified, child.raw}}); result.children.push_back(std::move(child));
    } else if (reader.xml.isCharacters()) {
      const auto text = reader.xml.text().toString(); result.text += text; content.append(text);
    } else if (reader.xml.isComment()) content.append(QJsonObject{{"comment", reader.xml.text().toString()}});
  }
  result.raw = {{"name", result.qualified}, {"namespace", uri}, {"attributes", attributes}, {"namespaces", namespaces}, {"content", content}};
  return result;
}
void quarantine(LegacyHistoryPreview &result, qsizetype index, const QJsonObject &fields, const QString &reason) {
  result.quarantine.append(QJsonObject{{"index", index}, {"record", fields}});
  result.report.add(compatibility::ImportDisposition::Quarantined);
  result.report.addRecord(index, "legacy-history", {}, compatibility::ImportDisposition::Quarantined, reason);
}
}
LegacyHistoryPreview previewLegacyHistoryXml(const QByteArray &bytes) {
  if (bytes.size() > kLegacyHistoryMaxBytes) return failed("legacy-history-too-large");
  if (bytes.startsWith(QByteArray::fromHex("fffe")) || bytes.startsWith(QByteArray::fromHex("feff")) ||
      bytes.startsWith(QByteArray::fromHex("0000feff"))) return failed("legacy-history-xml-unsupported-encoding");
  QByteArray payload = bytes;
  if (payload.startsWith(QByteArray::fromHex("efbbbf"))) payload.remove(0, 3);
  if (payload.startsWith("<?xml") && payload.size() > 5 && xmlSpace(payload.at(5))) {
    const auto end = payload.indexOf("?>");
    if (end < 0) return failed("legacy-history-xml-invalid");
    const auto declaration = payload.left(end + 2);
    const auto encoding = QRegularExpression("\\bencoding\\s*=\\s*(['\"])([^'\"]+)\\1").match(QString::fromLatin1(declaration));
    if (encoding.hasMatch() && encoding.captured(2).compare("UTF-8", Qt::CaseInsensitive) != 0 &&
        encoding.captured(2).compare("UTF8", Qt::CaseInsensitive) != 0) return failed("legacy-history-xml-unsupported-encoding");
    QXmlStreamReader header(declaration + "<XerahSLegacyDeclaration/>"); header.setEntityExpansionLimit(1);
    while (!header.atEnd()) header.readNext();
    if (header.hasError()) return failed("legacy-history-xml-invalid");
    payload.remove(0, end + 2);
  }
  QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
  const QString decoded = utf8(payload);
  Q_UNUSED(decoded);
  if (utf8.hasError()) return failed("legacy-history-xml-invalid-encoding");
  // Inspect the real prolog before adding a container for the append-only
  // fragment stream. DTDs never reach record parsing or normalization.
  QXmlStreamReader prolog(payload); prolog.setEntityExpansionLimit(1);
  while (!prolog.atEnd()) {
    const auto token = prolog.readNext();
    if (token == QXmlStreamReader::DTD) return failed("legacy-history-xml-dtd-forbidden");
    if (token == QXmlStreamReader::ProcessingInstruction) return failed("legacy-history-xml-processing-instruction");
    if (token == QXmlStreamReader::StartElement) break;
  }
  Reader reader("<XerahSLegacyFragments>" + payload + "</XerahSLegacyFragments>");
  LegacyHistoryPreview result; result.format = LegacyHistoryFormat::Xml; qsizetype ordinal = 0;
  const QMap<QString, QString> names{{"Filename", "FileName"}, {"Filepath", "FilePath"}, {"DateTimeUtc", "DateTime"},
      {"Type", "Type"}, {"Host", "Host"}, {"URL", "URL"}, {"ThumbnailURL", "ThumbnailURL"}, {"DeletionURL", "DeletionURL"}, {"ShortenedURL", "ShortenedURL"}};
  while (reader.next()) {
    if (reader.xml.isStartElement()) {
      if (reader.depth == 0) continue;
      if (reader.xml.qualifiedName() == QStringLiteral("HistoryItem")) {
        auto node = element(reader); if (!reader.code.isEmpty()) break;
        QJsonObject fields{{"LegacyXml", node.raw}}; bool duplicate = false;
        for (const auto &child : node.children) {
          const auto mapped = names.constFind(child.local);
          if (mapped == names.cend()) continue;
          if (fields.contains(*mapped)) duplicate = true;
          else fields.insert(*mapped, child.text);
        }
        if (node.unsafe || duplicate) quarantine(result, ordinal, fields, node.unsafe ? "unsafe-content" : "duplicate-field");
        else {
          const auto candidate = previewLegacyHistoryRecords(QJsonDocument(fields).toJson(QJsonDocument::Compact), LegacyHistoryFormat::Xml);
          if (!candidate.ok()) return failed(candidate.code);
          if (candidate.records.empty()) quarantine(result, ordinal, fields, candidate.report.records.empty() ? QStringLiteral("invalid-record") : candidate.report.records.front().reason);
          else { auto accepted = candidate.records.front(); accepted.index = ordinal; accepted.format = LegacyHistoryFormat::Xml; result.records.push_back(std::move(accepted)); result.report.add(compatibility::ImportDisposition::Retained); }
        }
        ++ordinal;
      } else {
        if (unsafeName(reader.xml.qualifiedName().toString(), reader.xml.name().toString(), reader.xml.namespaceUri().toString())) return failed("legacy-history-xml-unsafe-content");
        for (const auto &attribute : reader.xml.attributes())
          if (unsafeName(attribute.qualifiedName().toString(), attribute.name().toString(), attribute.namespaceUri().toString())) return failed("legacy-history-xml-unsafe-content");
      }
    } else if (reader.xml.isCharacters()) {
      auto outside = reader.xml.text().toString(); outside.remove(QChar(0xfeff));
      if (!outside.trimmed().isEmpty()) return failed("legacy-history-xml-unexpected-text");
    }
  }
  if (!reader.code.isEmpty()) return failed(reader.code);
  return result;
}
}
