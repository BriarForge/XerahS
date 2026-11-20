// EDITOR-UTILITIES-001 / EU-009, EU-010: the background-removal model supply.
// Qt Core only. The approved models and their pinned provenance live in the
// contract catalogue (background-model-catalogue.json); this file loads that
// data strictly, reads a model file's ONNX header with a bounded parser, and
// validates downloaded or user-supplied models against the catalogue's pins
// (EU-010: nothing is executed or kept from an unverified artifact).
#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QVector>

#include <memory>

namespace xerahs::bg {

// --- ONNX header types (ModelProto/GraphProto/ValueInfoProto subset) ---

struct TensorDim {
  qint64 value = -1;   // >= 1 when the dimension is a concrete size
  QString param;       // set when the dimension is symbolic ("dynamic")
  bool known() const { return value >= 0; }
};

struct TensorContract {
  QString name;                     // value name inside the graph
  int elemType = 0;                 // ONNX TensorProto.DataType (1 = float32)
  QVector<TensorDim> dims;

  // Strict match against the catalogue contract: names, element types and
  // every dimension must agree; a symbolic actual dimension does not satisfy
  // a pinned concrete size.
  bool matches(const TensorContract &expected) const;
  QString display() const;          // e.g. "float32[1,3,320,320]"
};

struct ModelHeader {
  qint64 irVersion = -1;
  QString producer, producerVersion, graphName;
  QList<TensorContract> inputs, outputs;
};

enum class HeaderStatus { Ok, Unreadable, TooLarge, Malformed, NoGraph, NoInputs, NoOutputs };

struct HeaderResult {
  HeaderStatus status = HeaderStatus::Ok;
  ModelHeader header;
  // Stable kebab-case code, empty on success: "model-unreadable",
  // "model-too-large", "model-header-malformed", "model-no-graph",
  // "model-no-inputs", "model-no-outputs".
  QString errorId() const;
  QString message() const;          // plain English, no paths
};

// Reads only the header fields of an ONNX file. Bounded: files above
// maxModelBytes() are refused, submessage lengths must lie inside their
// parent, value/dimension counts and string lengths are capped, and any
// structural violation is Malformed rather than an exception.
HeaderResult readModelHeader(const QString &path);
qint64 maxModelBytes();

// A local (user-supplied) model has no pinned checksums; the honest checks
// are structural: it must carry at least one float32 tensor input and one
// float32 tensor output. The exact inference contract is decided with the
// runtime build.
bool headerIsUsable(const ModelHeader &header);

// --- Contract catalogue (strict load) ---

struct CatalogueEntry {
  QString id;                       // "u2netp"
  QString label, description;
  struct Upstream {
    QString project, authors, paper, url;
  } upstream;
  struct Licence {
    QString id, url, note;
  } licence;
  struct Artifact {
    QString url, host, hostLicence, sha256, md5;
    qint64 sizeBytes = 0;
  } artifact;
  struct Preprocess {
    QString colorOrder, resize;     // "RGB", "320x320"
    double mean[3] = {0, 0, 0};
    double std[3] = {1, 1, 1};
  } preprocess;
  struct MaskPolicy {
    QString output, normalize, alpha, resize, color;
  } maskPolicy;
  TensorContract input, firstOutput;

  QString fileName() const;          // "u2netp.onnx"
};

struct CatalogueResult {
  bool ok = false;
  QList<CatalogueEntry> entries;
  QString error;                     // plain-English load diagnostic
};

CatalogueResult loadCatalogue(const QByteArray &json);
CatalogueResult loadCatalogueFromFile(const QString &path);

// --- Validation funnel and store (explicit directory, verify-then-adopt) ---

enum class ValidationStatus {
  Ok, Missing, Unreadable, TooLarge, SizeMismatch, ShaMismatch, MdMismatch,
  HeaderMalformed, NoGraph, NoInputs, NoOutputs, ContractMismatch
};

struct ValidationResult {
  ValidationStatus status = ValidationStatus::Ok;
  ModelHeader header;                // filled whenever the header parsed
  bool headerParsed = false;
  // Stable kebab-case code, empty on success: "model-missing",
  // "model-unreadable", "model-too-large", "model-size-mismatch",
  // "model-integrity-mismatch" (sha or md5), "model-header-malformed",
  // "model-no-graph", "model-no-inputs", "model-no-outputs",
  // "model-contract-mismatch".
  QString errorId() const;
  QString message() const;           // plain English, no paths
};

// Size, SHA-256, MD5 (streamed with tools::FileHash), then the ONNX header
// and its match with the catalogue's tensor contract. The strongest failure
// short-circuits, so no expensive step runs after a refusal.
ValidationResult validateModelFile(const QString &path, const CatalogueEntry &entry);

QString storeModelPath(const QString &storeDir, const QString &id);       // <dir>/u2netp.onnx
QString storeTempPath(const QString &storeDir, const QString &id);         // <dir>/u2netp.onnx.download

enum class StoreStatus { Ok, MkDirFailed };
StoreStatus ensureStoreDir(const QString &storeDir);

enum class AdoptStatus { Ok, NotVerified, MoveFailed };
// Adopts a downloaded temporary file into the store. validateModelFile runs
// first, so the final store path only ever holds a fully verified file; a
// previous model survives any failed adoption. detail receives the
// validation error id when the adoption is refused.
AdoptStatus adoptModel(const QString &tempPath, const QString &storeDir, const CatalogueEntry &entry,
                       QString *detail = nullptr);

// --- EU-011: the inference pipeline (Qt Core; QtGui supplies QImage only) ---

// Encodes the source image as the catalogue model's float32 input tensor:
// RGB, smooth-resized to the contract size, each channel scaled to the
// 0..1 domain the recorded mean/std are defined against (the original
// U-2-Net preprocessing; rembg divides by each image's own maximum instead,
// which the decision record does not adopt), then (x - mean) / std, packed
// CHW little-endian. The size always equals the entry's pinned input dims.
enum class PipelineStatus { Ok, ImageUnusable, ContractUnsupported, MaskInvalid, MemoryFailed };
struct PipelineInput {
  PipelineStatus status = PipelineStatus::Ok;
  QByteArray tensor;
  QString errorId() const;      // "" or "image-unusable" / "model-contract-mismatch" / "model-memory"
  QString message() const;
};
PipelineInput encodeInputTensor(const QImage &source, const CatalogueEntry &entry);

// Composes the alpha output from the raw first-output mask per the decided
// policy: min-max normalize (a constant mask is defined, not NaN: alpha 255
// when the constant is positive, else 0), floor to 0..255, smooth-resize to
// the source dimensions, and the source colour preserved with only the alpha
// byte replaced. Non-finite mask values are refused whole.
struct PipelineOutput {
  PipelineStatus status = PipelineStatus::Ok;
  QImage image;                 // ARGB32, source dimensions, defined alpha
  QString errorId() const;      // "" or "model-output-invalid" / "model-memory"
  QString message() const;
};
PipelineOutput composeMaskedImage(const QImage &source, const QVector<float> &rawMask,
                                  const CatalogueEntry &entry);

// --- EU-009/EU-011: the mask engine port ---
// The production implementation links the approved onnxruntime (MIT),
// optional at build time exactly like libgcrypt: without it the factory
// returns nullptr and the window reports the engine as not linked. Tests
// implement their own engine.

struct EngineInfo {
  bool linked = false;
  QString name;                 // e.g. "onnxruntime 1.29.0"
  bool cpu = false;
  bool gpu = false;             // a hardware execution provider is available
  QString detail;               // provider detail for the device row
};

enum class DevicePolicy { Auto, Cpu, Gpu };
QString devicePolicyId(DevicePolicy policy);       // "auto" / "cpu" / "gpu"
QString devicePolicyLabel(DevicePolicy policy);    // "Auto" / "CPU" / "GPU"

struct EngineResult {
  bool ok = false;
  bool cancelled = false;
  QImage image;                  // the composed preview/output on success
  // One of the EU-011 failure ids: "model-engine-open-failed" (model),
  // "model-device-unavailable" (device), "model-memory" (memory),
  // "model-inference-failed" (inference), "model-output-invalid" (output),
  // "model-contract-mismatch" (the session's contract disagrees with the
  // catalogue), "model-input-invalid" (the caller's tensor), or
  // "image-unusable"; "inference-cancelled" with cancelled=true.
  QString errorId, message;
};

class MaskEngine {
public:
  virtual ~MaskEngine();
  virtual EngineInfo info() const = 0;
  // The whole pipeline for one image: open the model (cached per path),
  // preprocess, run, compose. Runs off the GUI thread; callers pass a
  // detached copy of the source. The source is never modified.
  virtual EngineResult compute(const QImage &source, const QString &modelPath, const CatalogueEntry &entry,
                               DevicePolicy device) = 0;
  // Cooperative and cross-thread: a cancelled compute returns
  // cancelled=true without touching anything.
  virtual void cancel() = 0;
  virtual void resetCancel() = 0;
  virtual void close() = 0;
};

}  // namespace xerahs::bg
