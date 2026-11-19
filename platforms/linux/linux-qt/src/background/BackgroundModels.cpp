#include "BackgroundModels.h"

#include "tools/FileHash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>

#include <cmath>

namespace xerahs::bg {

namespace {

// --- Bounded protobuf wire reader (ONNX ModelProto subset) ---

constexpr qint64 kMaxModelBytes = qint64(512) * 1024 * 1024;   // 512 MiB hard ceiling
constexpr int kMaxGraphInputs = 16;
constexpr int kMaxGraphOutputs = 64;
constexpr int kMaxTensorDims = 8;
constexpr int kMaxNameBytes = 256;
constexpr int kMaxDimParamBytes = 64;
constexpr qint64 kMaxDimValue = qint64(1) << 40;

class WireReader final {
public:
  WireReader(const QByteArray &bytes) : m_bytes(bytes) {}

  bool atEnd() const { return m_pos >= m_bytes.size(); }
  qsizetype remaining() const { return m_bytes.size() - m_pos; }

  bool readVarint(qint64 *out) {
    quint64 value = 0;
    int shift = 0;
    while (true) {
      if (m_pos >= m_bytes.size() || shift >= 64) return false;
      const quint8 byte = static_cast<quint8>(m_bytes.at(m_pos++));
      value |= quint64(byte & 0x7F) << shift;
      if (!(byte & 0x80)) break;
      shift += 7;
    }
    *out = static_cast<qint64>(value);
    return true;
  }

  // Returns the declared length and positions the cursor at the payload.
  bool beginLengthDelimited(qint64 *length) {
    qint64 len = 0;
    if (!readVarint(&len) || len < 0 || len > remaining()) return false;
    *length = len;
    return true;
  }

  void skip(qint64 length) { m_pos += static_cast<qsizetype>(length); }

  // A copy of a payload range, for descending into a submessage that
  // readField already skipped past.
  QByteArray slice(qsizetype start, qint64 length) const {
    return m_bytes.mid(start, static_cast<qsizetype>(length));
  }

  qsizetype position() const { return m_pos; }

private:
  QByteArray m_bytes;
  qsizetype m_pos = 0;
};

struct Field {
  int number = 0;
  int wireType = 0;
  qint64 varint = 0;       // set when wireType == 0
  qint64 length = 0;       // set when wireType == 2
  qint64 payloadAt = 0;    // set when wireType == 2: offset of the payload
};

// Reads one field header. The cursor is always left AFTER the field's
// payload, so a caller that ignores a field has skipped it correctly.
bool readField(WireReader &reader, Field *field) {
  qint64 tag = 0;
  if (!reader.readVarint(&tag)) return false;
  field->number = static_cast<int>(tag >> 3);
  field->wireType = static_cast<int>(tag & 7);
  if (field->number <= 0) return false;
  switch (field->wireType) {
  case 0:
    return reader.readVarint(&field->varint);
  case 1:
    reader.skip(8);
    return true;
  case 2:
    if (!reader.beginLengthDelimited(&field->length)) return false;
    field->payloadAt = reader.position();
    reader.skip(field->length);
    return true;
  case 5:
    reader.skip(4);
    return true;
  default:   // 3, 4 (deprecated groups) and 6, 7 are not valid wire types
    return false;
  }
}

TensorContract parseTensorType(const QByteArray &sub) {
  // TypeProto.Tensor: field 1 elem_type (varint), field 2 shape (message).
  TensorContract contract;
  WireReader reader(sub);
  while (!reader.atEnd()) {
    Field field;
    if (!readField(reader, &field)) return {};
    if (field.number == 1 && field.wireType == 0) {
      contract.elemType = static_cast<int>(field.varint);
    } else if (field.number == 2 && field.wireType == 2) {
      const QByteArray shape = reader.slice(field.payloadAt, field.length);
      WireReader shapeReader(shape);
      while (!shapeReader.atEnd()) {
        if (contract.dims.size() >= kMaxTensorDims) return {};
        Field dimField;
        if (!readField(shapeReader, &dimField)) return {};
        if (dimField.number != 1 || dimField.wireType != 2) continue;
        const QByteArray dim = shapeReader.slice(dimField.payloadAt, dimField.length);
        TensorDim tensorDim;
        WireReader dimReader(dim);
        while (!dimReader.atEnd()) {
          Field part;
          if (!readField(dimReader, &part)) return {};
          if (part.number == 1 && part.wireType == 0) {
            if (part.varint < 1 || part.varint > kMaxDimValue) return {};
            tensorDim.value = part.varint;
          } else if (part.number == 2 && part.wireType == 2) {
            if (part.length > kMaxDimParamBytes) return {};
            tensorDim.param = QString::fromUtf8(dimReader.slice(part.payloadAt, part.length));
          }
        }
        contract.dims.append(tensorDim);
      }
    }
  }
  return contract;
}

TensorContract parseValueInfo(const QByteArray &sub, bool *ok) {
  // ValueInfoProto: field 1 name (string), field 2 type (TypeProto message).
  TensorContract contract;
  bool nameSeen = false, typeSeen = false;
  WireReader reader(sub);
  while (!reader.atEnd()) {
    Field field;
    if (!readField(reader, &field)) { *ok = false; return {}; }
    if (field.number == 1 && field.wireType == 2) {
      if (field.length > kMaxNameBytes) { *ok = false; return {}; }
      contract.name = QString::fromUtf8(reader.slice(field.payloadAt, field.length));
      nameSeen = true;
    } else if (field.number == 2 && field.wireType == 2) {
      // TypeProto: field 1 is the Tensor oneof member this product consumes.
      const QByteArray type = reader.slice(field.payloadAt, field.length);
      WireReader typeReader(type);
      while (!typeReader.atEnd()) {
        Field typeField;
        if (!readField(typeReader, &typeField)) { *ok = false; return {}; }
        if (typeField.number == 1 && typeField.wireType == 2) {
          const QByteArray tensor = typeReader.slice(typeField.payloadAt, typeField.length);
          const TensorContract parsed = parseTensorType(tensor);
          contract.elemType = parsed.elemType;
          contract.dims = parsed.dims;
          typeSeen = true;
        }
      }
    }
  }
  *ok = nameSeen && typeSeen;
  return contract;
}

HeaderResult parseModelProto(const QByteArray &bytes) {
  HeaderResult result;
  WireReader reader(bytes);
  bool graphSeen = false;
  QByteArray graphBytes;
  while (!reader.atEnd()) {
    Field field;
    if (!readField(reader, &field)) {
      result.status = HeaderStatus::Malformed;
      return result;
    }
    switch (field.number) {
    case 1:
      if (field.wireType != 0) { result.status = HeaderStatus::Malformed; return result; }
      result.header.irVersion = field.varint;
      break;
    case 2:
      if (field.wireType != 2 || field.length > kMaxNameBytes) { result.status = HeaderStatus::Malformed; return result; }
      result.header.producer = QString::fromUtf8(reader.slice(field.payloadAt, field.length));
      break;
    case 3:
      if (field.wireType != 2 || field.length > kMaxNameBytes) { result.status = HeaderStatus::Malformed; return result; }
      result.header.producerVersion = QString::fromUtf8(reader.slice(field.payloadAt, field.length));
      break;
    case 7:
      if (field.wireType != 2) { result.status = HeaderStatus::Malformed; return result; }
      graphBytes = reader.slice(field.payloadAt, field.length);
      graphSeen = true;
      break;
    default:
      break;   // producer metadata, opsets, functions: skipped by wire type
    }
  }
  if (!graphSeen) {
    result.status = HeaderStatus::NoGraph;
    return result;
  }
  WireReader graph(graphBytes);
  while (!graph.atEnd()) {
    Field field;
    if (!readField(graph, &field)) {
      result.status = HeaderStatus::Malformed;
      return result;
    }
    if (field.number == 2 && field.wireType == 2) {
      if (field.length > kMaxNameBytes) { result.status = HeaderStatus::Malformed; return result; }
      result.header.graphName = QString::fromUtf8(graph.slice(field.payloadAt, field.length));
    } else if (field.number == 11 && field.wireType == 2) {
      if (result.header.inputs.size() >= kMaxGraphInputs) { result.status = HeaderStatus::Malformed; return result; }
      const QByteArray value = graph.slice(field.payloadAt, field.length);
      bool ok = false;
      const TensorContract contract = parseValueInfo(value, &ok);
      if (!ok) { result.status = HeaderStatus::Malformed; return result; }
      result.header.inputs.append(contract);
    } else if (field.number == 12 && field.wireType == 2) {
      if (result.header.outputs.size() >= kMaxGraphOutputs) { result.status = HeaderStatus::Malformed; return result; }
      const QByteArray value = graph.slice(field.payloadAt, field.length);
      bool ok = false;
      const TensorContract contract = parseValueInfo(value, &ok);
      if (!ok) { result.status = HeaderStatus::Malformed; return result; }
      result.header.outputs.append(contract);
    }
    // Nodes, initializers, value_info and annotations are skipped by wire type.
  }
  if (result.header.inputs.isEmpty()) result.status = HeaderStatus::NoInputs;
  if (result.header.outputs.isEmpty() && result.status == HeaderStatus::Ok) result.status = HeaderStatus::NoOutputs;
  return result;
}

// --- Catalogue JSON helpers ---

bool requireString(const QJsonObject &object, const QString &key, QString *out, QString *error,
                   const QString &where) {
  const QJsonValue value = object.value(key);
  if (!value.isString() || value.toString().trimmed().isEmpty()) {
    *error = QStringLiteral("%1 must be a non-empty string").arg(where);
    return false;
  }
  *out = value.toString();
  return true;
}

bool requireHttps(const QString &url, QString *error, const QString &where) {
  const QUrl parsed(url);
  if (!parsed.isValid() || parsed.scheme() != QStringLiteral("https") || parsed.host().isEmpty()) {
    *error = QStringLiteral("%1 must be an https URL (found \"%2\")").arg(where, url);
    return false;
  }
  return true;
}

bool requireHex(const QString &value, int length, QString *error, const QString &where) {
  static const QRegularExpression hex(QStringLiteral("^[0-9a-f]+$"));
  if (value.length() != length || !hex.match(value).hasMatch()) {
    *error = QStringLiteral("%1 must be %2 lowercase hex digits").arg(where).arg(length);
    return false;
  }
  return true;
}

TensorContract parseContractJson(const QJsonObject &object, const QString &where, QString *error) {
  TensorContract contract;
  if (!requireString(object, QStringLiteral("name"), &contract.name, error, where + QStringLiteral(".name")))
    return {};
  if (contract.name.size() > kMaxNameBytes) {
    *error = QStringLiteral("%1.name is longer than %2 characters").arg(where).arg(kMaxNameBytes);
    return {};
  }
  QString elemType;
  if (!requireString(object, QStringLiteral("elem_type"), &elemType, error, where + QStringLiteral(".elem_type")))
    return {};
  if (elemType != QStringLiteral("float32")) {
    *error = QStringLiteral("%1.elem_type \"%2\" is not a supported element type (only float32)").arg(where, elemType);
    return {};
  }
  contract.elemType = 1;   // TensorProto.DataType.FLOAT
  const QJsonValue dims = object.value(QStringLiteral("dims"));
  if (!dims.isArray()) {
    *error = QStringLiteral("%1.dims must be an array").arg(where);
    return {};
  }
  const QJsonArray dimArray = dims.toArray();
  if (dimArray.isEmpty() || dimArray.size() > kMaxTensorDims) {
    *error = QStringLiteral("%1.dims must hold between 1 and %2 dimensions").arg(where).arg(kMaxTensorDims);
    return {};
  }
  for (const QJsonValue &dim : dimArray) {
    if (!dim.isDouble()) {
      *error = QStringLiteral("%1.dims pins only concrete sizes; found a non-integer dimension").arg(where);
      return {};
    }
    const double value = dim.toDouble();
    if (value < 1 || value > kMaxDimValue || value != std::floor(value)) {
      *error = QStringLiteral("%1.dims has an invalid size (%2)").arg(where).arg(value);
      return {};
    }
    TensorDim tensorDim;
    tensorDim.value = static_cast<qint64>(value);
    contract.dims.append(tensorDim);
  }
  return contract;
}

}  // namespace

// --- TensorContract ---

bool TensorContract::matches(const TensorContract &expected) const {
  if (name != expected.name || elemType != expected.elemType) return false;
  if (dims.size() != expected.dims.size()) return false;
  for (int i = 0; i < dims.size(); ++i) {
    if (!dims.at(i).known()) return false;                    // symbolic never satisfies a pin
    if (dims.at(i).value != expected.dims.at(i).value) return false;
  }
  return true;
}

QString TensorContract::display() const {
  QStringList parts;
  for (const TensorDim &dim : dims)
    parts.append(dim.known() ? QString::number(dim.value) : QStringLiteral("dynamic"));
  static const char *names[] = {"undefined", "float32", "uint8", "int8", "uint16", "int16", "int32", "int64"};
  const char *elem = elemType >= 0 && elemType <= 7 ? names[elemType] : "type";
  return QStringLiteral("%1 %2 [%3]").arg(elem, name, parts.join(QStringLiteral(", ")));
}

// --- Header reading ---

qint64 maxModelBytes() { return kMaxModelBytes; }

HeaderResult readModelHeader(const QString &path) {
  HeaderResult result;
  QFileInfo info(path);
  if (!info.exists()) {
    result.status = HeaderStatus::Unreadable;
    return result;
  }
  if (info.size() > kMaxModelBytes) {
    result.status = HeaderStatus::TooLarge;
    return result;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    result.status = HeaderStatus::Unreadable;
    return result;
  }
  const QByteArray bytes = file.readAll();
  if (bytes.size() != static_cast<qsizetype>(info.size())) {
    result.status = HeaderStatus::Unreadable;   // changed while reading
    return result;
  }
  return parseModelProto(bytes);
}

bool headerIsUsable(const ModelHeader &header) {
  return !header.inputs.isEmpty() && !header.outputs.isEmpty() &&
         header.inputs.first().elemType == 1 && header.outputs.first().elemType == 1;
}

QString HeaderResult::errorId() const {
  switch (status) {
  case HeaderStatus::Ok: return {};
  case HeaderStatus::Unreadable: return QStringLiteral("model-unreadable");
  case HeaderStatus::TooLarge: return QStringLiteral("model-too-large");
  case HeaderStatus::Malformed: return QStringLiteral("model-header-malformed");
  case HeaderStatus::NoGraph: return QStringLiteral("model-no-graph");
  case HeaderStatus::NoInputs: return QStringLiteral("model-no-inputs");
  case HeaderStatus::NoOutputs: return QStringLiteral("model-no-outputs");
  }
  return {};
}

QString HeaderResult::message() const {
  switch (status) {
  case HeaderStatus::Ok: return {};
  case HeaderStatus::Unreadable:
    return QStringLiteral("The model file could not be read.");
  case HeaderStatus::TooLarge:
    return QStringLiteral("The model file is larger than this build allows.");
  case HeaderStatus::Malformed:
    return QStringLiteral("The model file is not a well-formed ONNX model.");
  case HeaderStatus::NoGraph:
    return QStringLiteral("The model file contains no computation graph.");
  case HeaderStatus::NoInputs:
    return QStringLiteral("The model declares no tensor inputs.");
  case HeaderStatus::NoOutputs:
    return QStringLiteral("The model declares no tensor outputs.");
  }
  return {};
}

// --- Catalogue loading ---

QString CatalogueEntry::fileName() const { return id + QStringLiteral(".onnx"); }

CatalogueResult loadCatalogueFromFile(const QString &path) {
  CatalogueResult result;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    result.error = QStringLiteral("The background-model catalogue could not be read.");
    return result;
  }
  return loadCatalogue(file.readAll());
}

CatalogueResult loadCatalogue(const QByteArray &json) {
  CatalogueResult result;
  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    result.error = QStringLiteral("The background-model catalogue is not valid JSON.");
    return result;
  }
  const QJsonObject root = document.object();
  if (root.value(QStringLiteral("schema_version")).toInt() != 1) {
    result.error = QStringLiteral("The background-model catalogue has an unsupported schema version.");
    return result;
  }
  const QJsonValue models = root.value(QStringLiteral("models"));
  if (!models.isArray() || models.toArray().isEmpty()) {
    result.error = QStringLiteral("The background-model catalogue lists no models.");
    return result;
  }
  static const QRegularExpression idPattern(QStringLiteral("^[a-z0-9][a-z0-9-]{0,63}$"));
  for (int index = 0; index < models.toArray().size(); ++index) {
    const QJsonObject model = models.toArray().at(index).toObject();
    CatalogueEntry entry;
    QString where = QStringLiteral("models[%1]").arg(index);
    QString error;
    if (!requireString(model, QStringLiteral("id"), &entry.id, &error, where + QStringLiteral(".id")) ||
        !idPattern.match(entry.id).hasMatch()) {
      result.error = error.isEmpty()
                         ? QStringLiteral("%1.id must be lowercase letters, digits or dashes").arg(where)
                         : error;
      return result;
    }
    if (!requireString(model, QStringLiteral("label"), &entry.label, &error, where + QStringLiteral(".label")) ||
        !requireString(model, QStringLiteral("description"), &entry.description, &error, where + QStringLiteral(".description"))) {
      result.error = error;
      return result;
    }
    const QJsonObject upstream = model.value(QStringLiteral("upstream")).toObject();
    where = QStringLiteral("models[%1].upstream").arg(index);
    if (!requireString(upstream, QStringLiteral("project"), &entry.upstream.project, &error, where + QStringLiteral(".project")) ||
        !requireString(upstream, QStringLiteral("authors"), &entry.upstream.authors, &error, where + QStringLiteral(".authors")) ||
        !requireString(upstream, QStringLiteral("paper"), &entry.upstream.paper, &error, where + QStringLiteral(".paper")) ||
        !requireString(upstream, QStringLiteral("url"), &entry.upstream.url, &error, where + QStringLiteral(".url")) ||
        !requireHttps(entry.upstream.url, &error, where + QStringLiteral(".url"))) {
      result.error = error;
      return result;
    }
    const QJsonObject licence = model.value(QStringLiteral("licence")).toObject();
    where = QStringLiteral("models[%1].licence").arg(index);
    if (!requireString(licence, QStringLiteral("id"), &entry.licence.id, &error, where + QStringLiteral(".id")) ||
        !requireString(licence, QStringLiteral("url"), &entry.licence.url, &error, where + QStringLiteral(".url")) ||
        !requireHttps(entry.licence.url, &error, where + QStringLiteral(".url")) ||
        !requireString(licence, QStringLiteral("note"), &entry.licence.note, &error, where + QStringLiteral(".note"))) {
      result.error = error;
      return result;
    }
    const QJsonObject artifact = model.value(QStringLiteral("artifact")).toObject();
    where = QStringLiteral("models[%1].artifact").arg(index);
    if (!requireString(artifact, QStringLiteral("url"), &entry.artifact.url, &error, where + QStringLiteral(".url")) ||
        !requireHttps(entry.artifact.url, &error, where + QStringLiteral(".url")) ||
        !requireString(artifact, QStringLiteral("host"), &entry.artifact.host, &error, where + QStringLiteral(".host")) ||
        !requireString(artifact, QStringLiteral("host_licence"), &entry.artifact.hostLicence, &error, where + QStringLiteral(".host_licence")) ||
        !requireString(artifact, QStringLiteral("sha256"), &entry.artifact.sha256, &error, where + QStringLiteral(".sha256")) ||
        !requireHex(entry.artifact.sha256, 64, &error, where + QStringLiteral(".sha256")) ||
        !requireString(artifact, QStringLiteral("md5"), &entry.artifact.md5, &error, where + QStringLiteral(".md5")) ||
        !requireHex(entry.artifact.md5, 32, &error, where + QStringLiteral(".md5"))) {
      result.error = error;
      return result;
    }
    if (QUrl(entry.artifact.url).host() != entry.artifact.host) {
      result.error = QStringLiteral("%1.host must match the URL host").arg(where);
      return result;
    }
    const QJsonValue size = artifact.value(QStringLiteral("size_bytes"));
    if (!size.isDouble() || size.toDouble() <= 0 || size.toDouble() > kMaxModelBytes ||
        size.toDouble() != std::floor(size.toDouble())) {
      result.error = QStringLiteral("%1.size_bytes must be a positive integer within the model size ceiling").arg(where);
      return result;
    }
    entry.artifact.sizeBytes = static_cast<qint64>(size.toDouble());

    const QJsonObject contract = model.value(QStringLiteral("tensor_contract")).toObject();
    where = QStringLiteral("models[%1].tensor_contract").arg(index);
    entry.input = parseContractJson(contract.value(QStringLiteral("input")).toObject(),
                                   where + QStringLiteral(".input"), &error);
    if (!error.isEmpty()) {
      result.error = error;
      return result;
    }
    entry.firstOutput = parseContractJson(contract.value(QStringLiteral("first_output")).toObject(),
                                          where + QStringLiteral(".first_output"), &error);
    if (!error.isEmpty()) {
      result.error = error;
      return result;
    }

    const QJsonObject preprocess = model.value(QStringLiteral("preprocess")).toObject();
    where = QStringLiteral("models[%1].preprocess").arg(index);
    if (!requireString(preprocess, QStringLiteral("color_order"), &entry.preprocess.colorOrder, &error, where + QStringLiteral(".color_order")) ||
        !requireString(preprocess, QStringLiteral("resize"), &entry.preprocess.resize, &error, where + QStringLiteral(".resize"))) {
      result.error = error;
      return result;
    }
    for (int channel = 0; channel < 3; ++channel) {
      const QJsonValue mean = preprocess.value(QStringLiteral("mean")).toArray().at(channel);
      const QJsonValue dev = preprocess.value(QStringLiteral("std")).toArray().at(channel);
      if (!mean.isDouble() || !dev.isDouble() || dev.toDouble() <= 0) {
        result.error = QStringLiteral("%1.mean/std must list three numbers, each std > 0").arg(where);
        return result;
      }
      entry.preprocess.mean[channel] = mean.toDouble();
      entry.preprocess.std[channel] = dev.toDouble();
    }

    const QJsonObject mask = model.value(QStringLiteral("mask_policy")).toObject();
    where = QStringLiteral("models[%1].mask_policy").arg(index);
    if (!requireString(mask, QStringLiteral("output"), &entry.maskPolicy.output, &error, where + QStringLiteral(".output")) ||
        !requireString(mask, QStringLiteral("normalize"), &entry.maskPolicy.normalize, &error, where + QStringLiteral(".normalize")) ||
        !requireString(mask, QStringLiteral("alpha"), &entry.maskPolicy.alpha, &error, where + QStringLiteral(".alpha")) ||
        !requireString(mask, QStringLiteral("resize"), &entry.maskPolicy.resize, &error, where + QStringLiteral(".resize")) ||
        !requireString(mask, QStringLiteral("color"), &entry.maskPolicy.color, &error, where + QStringLiteral(".color"))) {
      result.error = error;
      return result;
    }
    result.entries.append(entry);
  }
  result.ok = true;
  return result;
}

// --- Validation funnel and store ---

QString ValidationResult::errorId() const {
  switch (status) {
  case ValidationStatus::Ok: return {};
  case ValidationStatus::Missing: return QStringLiteral("model-missing");
  case ValidationStatus::Unreadable: return QStringLiteral("model-unreadable");
  case ValidationStatus::TooLarge: return QStringLiteral("model-too-large");
  case ValidationStatus::SizeMismatch: return QStringLiteral("model-size-mismatch");
  case ValidationStatus::ShaMismatch:
  case ValidationStatus::MdMismatch: return QStringLiteral("model-integrity-mismatch");
  case ValidationStatus::HeaderMalformed: return QStringLiteral("model-header-malformed");
  case ValidationStatus::NoGraph: return QStringLiteral("model-no-graph");
  case ValidationStatus::NoInputs: return QStringLiteral("model-no-inputs");
  case ValidationStatus::NoOutputs: return QStringLiteral("model-no-outputs");
  case ValidationStatus::ContractMismatch: return QStringLiteral("model-contract-mismatch");
  }
  return {};
}

QString ValidationResult::message() const {
  switch (status) {
  case ValidationStatus::Ok: return {};
  case ValidationStatus::Missing:
    return QStringLiteral("The model file is not in the store yet.");
  case ValidationStatus::Unreadable:
    return QStringLiteral("The model file could not be read.");
  case ValidationStatus::TooLarge:
    return QStringLiteral("The model file is larger than this build allows.");
  case ValidationStatus::SizeMismatch:
    return QStringLiteral("The model file's size does not match the approved model.");
  case ValidationStatus::ShaMismatch:
  case ValidationStatus::MdMismatch:
    return QStringLiteral("The model file failed its integrity check against the approved checksums.");
  case ValidationStatus::HeaderMalformed:
    return QStringLiteral("The model file is not a well-formed ONNX model.");
  case ValidationStatus::NoGraph:
    return QStringLiteral("The model file contains no computation graph.");
  case ValidationStatus::NoInputs:
    return QStringLiteral("The model declares no tensor inputs.");
  case ValidationStatus::NoOutputs:
    return QStringLiteral("The model declares no tensor outputs.");
  case ValidationStatus::ContractMismatch:
    return QStringLiteral("The model's tensor contract does not match the approved contract.");
  }
  return {};
}

ValidationResult validateModelFile(const QString &path, const CatalogueEntry &entry) {
  ValidationResult result;
  QFileInfo info(path);
  if (!info.exists()) {
    result.status = ValidationStatus::Missing;
    return result;
  }
  if (info.size() > kMaxModelBytes) {
    result.status = ValidationStatus::TooLarge;
    return result;
  }
  if (info.size() != entry.artifact.sizeBytes) {
    result.status = ValidationStatus::SizeMismatch;
    return result;
  }
  const auto sha = tools::hashFile(path, tools::HashAlgorithm::Sha256);
  if (!sha.ok) {
    result.status = ValidationStatus::Unreadable;
    return result;
  }
  if (!tools::digestMatches(sha.hex, entry.artifact.sha256)) {
    result.status = ValidationStatus::ShaMismatch;
    return result;
  }
  const auto md5 = tools::hashFile(path, tools::HashAlgorithm::Md5);
  if (!md5.ok) {
    result.status = ValidationStatus::Unreadable;
    return result;
  }
  if (!tools::digestMatches(md5.hex, entry.artifact.md5)) {
    result.status = ValidationStatus::MdMismatch;
    return result;
  }
  const HeaderResult header = readModelHeader(path);
  if (header.status != HeaderStatus::Ok) {
    switch (header.status) {
    case HeaderStatus::Unreadable: result.status = ValidationStatus::Unreadable; break;
    case HeaderStatus::TooLarge: result.status = ValidationStatus::TooLarge; break;
    case HeaderStatus::Malformed: result.status = ValidationStatus::HeaderMalformed; break;
    case HeaderStatus::NoGraph: result.status = ValidationStatus::NoGraph; break;
    case HeaderStatus::NoInputs: result.status = ValidationStatus::NoInputs; break;
    case HeaderStatus::NoOutputs: result.status = ValidationStatus::NoOutputs; break;
    case HeaderStatus::Ok: break;
    }
    return result;
  }
  result.header = header.header;
  result.headerParsed = true;
  if (!header.header.inputs.first().matches(entry.input) ||
      !header.header.outputs.first().matches(entry.firstOutput)) {
    result.status = ValidationStatus::ContractMismatch;
    return result;
  }
  result.status = ValidationStatus::Ok;
  return result;
}

QString storeModelPath(const QString &storeDir, const QString &id) {
  return QDir(storeDir).filePath(id + QStringLiteral(".onnx"));
}

QString storeTempPath(const QString &storeDir, const QString &id) {
  return QDir(storeDir).filePath(id + QStringLiteral(".onnx.download"));
}

StoreStatus ensureStoreDir(const QString &storeDir) {
  return QDir().mkpath(storeDir) ? StoreStatus::Ok : StoreStatus::MkDirFailed;
}

AdoptStatus adoptModel(const QString &tempPath, const QString &storeDir, const CatalogueEntry &entry,
                       QString *detail) {
  const ValidationResult validation = validateModelFile(tempPath, entry);
  if (validation.status != ValidationStatus::Ok) {
    if (detail) *detail = validation.errorId();
    return AdoptStatus::NotVerified;
  }
  const StoreStatus store = ensureStoreDir(storeDir);
  if (store != StoreStatus::Ok) {
    if (detail) *detail = QStringLiteral("store-dir-failed");
    return AdoptStatus::MoveFailed;
  }
  const QString finalPath = storeModelPath(storeDir, entry.id);
  // The final path only ever holds a verified file: the old model, if any,
  // is removed only after the new bytes passed validation, and the rename
  // follows immediately.
  QFile previous(finalPath);
  if (previous.exists() && !previous.remove()) {
    if (detail) *detail = QStringLiteral("store-dir-failed");
    return AdoptStatus::MoveFailed;
  }
  QFile file(tempPath);
  if (!file.rename(finalPath)) {
    if (detail) *detail = QStringLiteral("store-dir-failed");
    return AdoptStatus::MoveFailed;
  }
  return AdoptStatus::Ok;
}

}  // namespace xerahs::bg
