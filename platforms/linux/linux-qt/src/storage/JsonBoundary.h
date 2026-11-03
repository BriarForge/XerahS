// COMPCAT-015: preflight before Qt builds an object (which would erase duplicate
// keys). No input value or key is ever incorporated into a diagnostic.
#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStringDecoder>
#include <vector>

namespace xerahs::compatibility {
struct JsonLimits {
  qint64 maxBytes = 1048576;
  int maxDepth = 64;
  qint64 maxElements = 100000;
};
struct JsonRead {
  QJsonDocument document;
  QString code;
  bool ok() const { return code.isEmpty(); }
};
inline JsonRead readJson(const QByteArray &bytes, JsonLimits limits = {}) {
  const auto fail = [](const char *code) { return JsonRead{{}, QString::fromLatin1(code)}; };
  if (limits.maxBytes < 0 || bytes.size() > limits.maxBytes) return fail("too-large");
  // Validate without a full decoded copy. The decoder remembers partial UTF-8
  // sequences across chunks; a final round-trip check covers incomplete tails.
  QStringDecoder utf8(QStringDecoder::Utf8);
  for (qsizetype offset = 0; offset < bytes.size(); offset += 4096) {
    const QString decoded = utf8.decode(QByteArrayView(bytes).sliced(offset, qMin<qsizetype>(4096, bytes.size() - offset)));
    Q_UNUSED(decoded);
    if (utf8.hasError()) return fail("invalid-encoding");
  }
  // Qt's streaming decoder defers incomplete tails until more input. Inspect
  // the final code point independently to refuse truncated multibyte input.
  if (!bytes.isEmpty()) {
    qsizetype start = bytes.size() - 1;
    while (start > 0 && (static_cast<unsigned char>(bytes[start]) & 0xc0) == 0x80) --start;
    const unsigned char lead = static_cast<unsigned char>(bytes[start]);
    const int expected = lead < 0x80 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 : lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
    if (!expected || bytes.size() - start != expected) return fail("invalid-encoding");
  }
  struct Frame { char opener; bool key = true; QSet<QString> keys; };
  std::vector<Frame> stack;
  qint64 elements = 0;
  const auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  const auto codeUnit = [&](qsizetype pos) -> int {
    if (pos + 4 > bytes.size()) return -1;
    int unit = 0;
    for (int n = 0; n < 4; ++n) { const int digit = hex(bytes[pos + n]); if (digit < 0) return -1; unit = unit * 16 + digit; }
    return unit;
  };
  for (qsizetype i = 0; i < bytes.size(); ++i) {
    const char c = bytes[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    if (c == '{' || c == '[') {
      if (++elements > limits.maxElements) return fail("too-many-elements");
      if (static_cast<int>(stack.size()) >= limits.maxDepth || stack.size() >= 256) return fail("too-deep");
      stack.push_back({c, true, {}});
    } else if (c == '}' || c == ']') {
      if (stack.empty() || stack.back().opener != (c == '}' ? '{' : '[')) return fail("invalid-json");
      stack.pop_back();
    } else if (c == ',') {
      if (!stack.empty()) stack.back().key = true;
    } else if (c == ':') {
      if (!stack.empty()) stack.back().key = false;
    } else if (c == '"') {
      if (++elements > limits.maxElements) return fail("too-many-elements");
      const qsizetype begin = i;
      bool closed = false;
      while (++i < bytes.size()) {
        const auto s = static_cast<unsigned char>(bytes[i]);
        if (s == '"') { closed = true; break; }
        if (s < 0x20) return fail("invalid-json");
        if (s != '\\') continue;
        if (++i >= bytes.size()) return fail("invalid-json");
        const char escape = bytes[i];
        if (escape == 'u') {
          const int unit = codeUnit(i + 1);
          if (unit < 0) return fail("invalid-json");
          i += 4;
          if (unit >= 0xd800 && unit <= 0xdbff) {
            if (i + 6 >= bytes.size() || bytes[i + 1] != '\\' || bytes[i + 2] != 'u') return fail("invalid-encoding");
            const int low = codeUnit(i + 3);
            if (low < 0xdc00 || low > 0xdfff) return fail("invalid-encoding");
            i += 6;
          } else if (unit >= 0xdc00 && unit <= 0xdfff) return fail("invalid-encoding");
        } else if (escape != '"' && escape != '\\' && escape != '/' && escape != 'b' && escape != 'f' && escape != 'n' && escape != 'r' && escape != 't') {
          return fail("invalid-json");
        }
      }
      if (!closed) return fail("invalid-json");
      if (!stack.empty() && stack.back().opener == '{' && stack.back().key) {
        const auto keyDocument = QJsonDocument::fromJson('[' + bytes.mid(begin, i - begin + 1) + ']');
        if (!keyDocument.isArray() || !keyDocument.array().at(0).isString()) return fail("invalid-json");
        const auto key = keyDocument.array().at(0).toString();
        if (stack.back().keys.contains(key)) return fail("duplicate-key");
        stack.back().keys.insert(key);
      }
    } else {
      if (++elements > limits.maxElements) return fail("too-many-elements");
      // Numbers/literals are validated by Qt after the bounded lexical pass.
      while (i + 1 < bytes.size()) {
        const char next = bytes[i + 1];
        if (next == ',' || next == '}' || next == ']' || next == ':' || next == '{' || next == '[' || next == '"' || next == ' ' || next == '\t' || next == '\r' || next == '\n') break;
        ++i;
      }
    }
  }
  if (!stack.empty()) return fail("invalid-json");
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || document.isNull()) return fail("invalid-json");
  return {document, {}};
}
} // namespace xerahs::compatibility
