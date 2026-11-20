// Hand-encoder for minimal ONNX ModelProto bytes (EDITOR-UTILITIES-001 /
// EU-009, EU-010 tests). ONNX is protobuf: this helper writes the exact
// subset the product parser reads - ModelProto(7=GraphProto),
// GraphProto(2=name, 11=input, 12=output), ValueInfoProto(1=name, 2=type),
// TypeProto(1=Tensor), Tensor(1=elem_type, 2=Shape), Shape(1=Dimension),
// Dimension(1=dim_value, 2=dim_param) - so tests never need a network, a
// real model file or a protobuf dependency.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace OnnxFixture {

struct Tensor {
  QString name;
  int elemType = 1;                        // 1 = float32
  QList<qint64> dims;                      // empty entries: dynamic dim_param
  QStringList params;                      // symbolic names, parallel to a dynamic dim
};

inline void putVarint(QByteArray *out, quint64 value) {
  while (value >= 0x80) {
    out->append(char((value & 0x7F) | 0x80));
    value >>= 7;
  }
  out->append(char(value));
}

inline void putTag(QByteArray *out, int field, int wireType) { putVarint(out, quint64(field) << 3 | quint64(wireType)); }

inline void putVarintField(QByteArray *out, int field, quint64 value) {
  putTag(out, field, 0);
  putVarint(out, value);
}

inline void putBytesField(QByteArray *out, int field, const QByteArray &bytes) {
  putTag(out, field, 2);
  putVarint(out, quint64(bytes.size()));
  out->append(bytes);
}

inline void putStringField(QByteArray *out, int field, const QString &text) {
  putBytesField(out, field, text.toUtf8());
}

inline QByteArray encodeTensor(const Tensor &tensor) {
  // TypeProto.Tensor { elem_type = 1, shape = TensorShapeProto }
  QByteArray shape;
  for (int i = 0; i < tensor.dims.size(); ++i) {
    QByteArray dimension;
    if (tensor.dims.at(i) > 0) putVarintField(&dimension, 1, quint64(tensor.dims.at(i)));
    else putStringField(&dimension, 2, tensor.params.value(i));
    putBytesField(&shape, 1, dimension);
  }
  QByteArray typeTensor;
  putVarintField(&typeTensor, 1, quint64(tensor.elemType));
  putBytesField(&typeTensor, 2, shape);
  QByteArray type;
  putBytesField(&type, 1, typeTensor);
  QByteArray valueInfo;
  putStringField(&valueInfo, 1, tensor.name);
  putBytesField(&valueInfo, 2, type);
  return valueInfo;
}

inline QByteArray encodeModel(const QList<Tensor> &inputs, const QList<Tensor> &outputs,
                              const QString &graphName = QStringLiteral("fixture-graph"),
                              const QByteArray &trailingInitializer = {}) {
  QByteArray graph;
  putStringField(&graph, 2, graphName);
  if (!trailingInitializer.isEmpty()) putBytesField(&graph, 5, trailingInitializer);
  for (const Tensor &input : inputs) putBytesField(&graph, 11, encodeTensor(input));
  for (const Tensor &output : outputs) putBytesField(&graph, 12, encodeTensor(output));
  QByteArray model;
  putVarintField(&model, 1, 6);   // ir_version 6, like the pinned artifact
  putStringField(&model, 2, QStringLiteral("fixture"));
  putStringField(&model, 3, QStringLiteral("1.0"));
  putBytesField(&model, 7, graph);
  return model;
}

// The exact tensor contract of the approved u2netp artifact.
inline Tensor u2netpInput() { return {QStringLiteral("input.1"), 1, {1, 3, 320, 320}, {}}; }
inline Tensor u2netpFirstOutput() { return {QStringLiteral("1959"), 1, {1, 1, 320, 320}, {}}; }

inline QByteArray u2netpLikeModel() {
  const QList<Tensor> inputs{u2netpInput()};
  const QList<Tensor> outputs{u2netpFirstOutput(),
                              {QStringLiteral("1960"), 1, {1, 1, 320, 320}, {}},
                              {QStringLiteral("1961"), 1, {0, 0, 0, 0},
                               {QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")}}};
  return encodeModel(inputs, outputs, QStringLiteral("torch-jit-export"));
}

// --- An executable model: real tensor I/O through a real inference runtime ---

inline void putAttributeInt(QByteArray *node, const char *name, quint64 value) {
  QByteArray attribute;
  putStringField(&attribute, 1, QLatin1String(name));   // AttributeProto.name
  putVarintField(&attribute, 3, value);                  // .i
  putVarintField(&attribute, 20, 2);                     // .type = INT (2)
  putBytesField(node, 5, attribute);
}

inline void putAttributeInts(QByteArray *node, const char *name, const QList<quint64> &values) {
  QByteArray attribute;
  putStringField(&attribute, 1, QLatin1String(name));    // AttributeProto.name
  QByteArray packed;                                      // .ints (field 8, packed varints)
  for (quint64 value : values) putVarint(&packed, value);
  putBytesField(&attribute, 8, packed);
  putVarintField(&attribute, 20, 7);                     // .type = INTS (7)
  putBytesField(node, 5, attribute);
}

inline void putOpset(QByteArray *model, quint64 version) {
  QByteArray opset;
  putStringField(&opset, 1, QString());                  // domain "" (the ONNX set)
  putVarintField(&opset, 2, version);
  putBytesField(model, 8, opset);
}

// A deterministic one-node model a real runtime executes: the mask is the mean
// over the channel axis of the normalized input, so tests know every expected
// output value from first principles. The graph contract matches u2netp
// (input.1 [1,3,320,320] float32 -> first output 1959 [1,1,320,320] float32);
// the names are parameters so a mismatch variant can be encoded for tests.
inline QByteArray encodeReduceMeanModel(const QString &inputName = QStringLiteral("input.1"),
                                        const QString &outputName = QStringLiteral("1959")) {
  QByteArray node;
  putStringField(&node, 1, inputName);                   // NodeProto.input
  putStringField(&node, 2, outputName);                 // NodeProto.output
  putStringField(&node, 4, QStringLiteral("ReduceMean"));  // .op_type
  putAttributeInts(&node, "axes", {1});
  putAttributeInt(&node, "keepdims", 1);

  Tensor input = u2netpInput();
  input.name = inputName;
  Tensor output = u2netpFirstOutput();
  output.name = outputName;

  QByteArray graph;
  putBytesField(&graph, 1, node);
  putStringField(&graph, 2, QStringLiteral("fixture-mean"));
  putBytesField(&graph, 11, encodeTensor(input));
  putBytesField(&graph, 12, encodeTensor(output));

  QByteArray model;
  putVarintField(&model, 1, 6);                          // ir_version
  putStringField(&model, 2, QStringLiteral("fixture"));
  putBytesField(&model, 7, graph);
  putOpset(&model, 13);
  return model;
}

}  // namespace OnnxFixture
