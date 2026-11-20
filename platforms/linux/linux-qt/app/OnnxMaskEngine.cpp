#include "OnnxMaskEngine.h"

#ifdef XERAHS_ONNXRUNTIME

#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <onnxruntime_cxx_api.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

namespace xerahs::app {
namespace {

bool providersContain(const std::vector<std::string> &providers, const char *name) {
  for (const std::string &provider : providers)
    if (provider == name) return true;
  return false;
}

// The first token of the runtime's version string, e.g. "1.29.0".
QString runtimeVersion() {
  const char *raw = OrtGetApiBase()->GetVersionString();
  return QString::fromLatin1(raw ? raw : "").section(QLatin1Char(' '), 0, 0);
}

qint64 elementCount(const xerahs::bg::TensorContract &contract) {
  qint64 count = 1;
  for (const xerahs::bg::TensorDim &dim : contract.dims) {
    if (!dim.known()) return -1;
    count *= dim.value;
  }
  return count;
}

std::vector<std::int64_t> shapeVector(const xerahs::bg::TensorContract &contract) {
  std::vector<std::int64_t> shape;
  for (const xerahs::bg::TensorDim &dim : contract.dims) shape.push_back(dim.value);
  return shape;
}

bool shapeMatches(const Ort::TypeInfo &type, const xerahs::bg::TensorContract &expected) {
  if (type.GetONNXType() != ONNX_TYPE_TENSOR) return false;
  const auto info = type.GetTensorTypeAndShapeInfo();
  return info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT && info.GetShape() == shapeVector(expected);
}

xerahs::bg::EngineResult cancelledResult() {
  xerahs::bg::EngineResult result;
  result.cancelled = true;
  result.errorId = QStringLiteral("inference-cancelled");
  result.message = QStringLiteral("Background removal was cancelled.");
  return result;
}

xerahs::bg::EngineResult failedResult(const QString &errorId, const QString &message) {
  xerahs::bg::EngineResult result;
  result.errorId = errorId;
  result.message = message;
  return result;
}

}  // namespace

// EU-009/EU-011: one cached session, one cooperative cancellation flag, and
// a live RunOptions registration so a cancel from the GUI thread terminates
// an in-flight Run. Every heavy call happens on the caller's worker thread;
// compute() holds the session mutex for its whole run, so close() can never
// destroy a session underneath a live Run.
class OnnxMaskEngine final : public xerahs::bg::MaskEngine {
public:
  OnnxMaskEngine() : m_env(ORT_LOGGING_LEVEL_WARNING, "xerahs") {}

  xerahs::bg::EngineInfo info() const override {
    xerahs::bg::EngineInfo result;
    result.linked = true;
    result.name = QStringLiteral("onnxruntime %1").arg(runtimeVersion());
    const std::vector<std::string> providers = Ort::GetAvailableProviders();
    result.cpu = providersContain(providers, "CPUExecutionProvider");
    result.gpu = providersContain(providers, "CUDAExecutionProvider") ||
                 providersContain(providers, "ROCmExecutionProvider") ||
                 providersContain(providers, "TensorrtExecutionProvider");
    QStringList names;
    for (const std::string &provider : providers) names.append(QString::fromStdString(provider));
    result.detail = names.join(QStringLiteral(", "));
    return result;
  }

  xerahs::bg::EngineResult compute(const QImage &source, const QString &modelPath,
                                   const xerahs::bg::CatalogueEntry &entry,
                                   xerahs::bg::DevicePolicy device) override {
    if (m_cancel.load()) return cancelledResult();
    if (source.isNull())
      return failedResult(QStringLiteral("image-unusable"),
                          QStringLiteral("The image could not be read."));

    // One lock covers open, run and compose, so the cached session is used by
    // exactly one thread at a time and close() waits for a settled run.
    QMutexLocker sessionLock(&m_mutex);
    QString errorId, message;
    if (!openSession(modelPath, entry, device, &errorId, &message)) return failedResult(errorId, message);
    if (m_cancel.load()) return cancelledResult();

    const xerahs::bg::PipelineInput input = xerahs::bg::encodeInputTensor(source, entry);
    if (input.status != xerahs::bg::PipelineStatus::Ok) return failedResult(input.errorId(), input.message());
    if (m_cancel.load()) return cancelledResult();

    const qint64 inputCount = elementCount(entry.input);
    if (inputCount <= 0 || qsizetype(inputCount) * qsizetype(sizeof(float)) != input.tensor.size()) {
      return failedResult(QStringLiteral("model-input-invalid"),
                         QStringLiteral("The prepared tensor does not match the model input."));
    }

    // One Run, registered so cancel() can terminate it mid-flight.
    Ort::RunOptions runOptions;
    {
      QMutexLocker runLock(&m_runMutex);
      if (m_cancel.load()) return cancelledResult();
      m_liveRun = &runOptions;
    }
    std::vector<Ort::Value> outputs;
    try {
      const std::int64_t shape[4] = {entry.input.dims.at(0).value, entry.input.dims.at(1).value,
                                     entry.input.dims.at(2).value, entry.input.dims.at(3).value};
      auto tensor = Ort::Value::CreateTensor<float>(
          m_memoryInfo, reinterpret_cast<float *>(const_cast<char *>(input.tensor.data())),
          std::size_t(inputCount), shape, 4);
      const char *inputNames[] = {m_inputName.constData()};
      const char *outputNames[] = {m_outputName.constData()};
      outputs = m_session->Run(runOptions, inputNames, &tensor, 1, outputNames, 1);
    } catch (const Ort::Exception &) {
      {
        QMutexLocker runLock(&m_runMutex);
        m_liveRun = nullptr;
      }
      if (m_cancel.load()) return cancelledResult();
      return failedResult(QStringLiteral("model-inference-failed"),
                          QStringLiteral("The model run failed."));
    } catch (const std::bad_alloc &) {
      {
        QMutexLocker runLock(&m_runMutex);
        m_liveRun = nullptr;
      }
      return failedResult(QStringLiteral("model-memory"),
                          QStringLiteral("Not enough memory for the model run."));
    }
    {
      QMutexLocker runLock(&m_runMutex);
      m_liveRun = nullptr;
    }
    if (m_cancel.load()) return cancelledResult();

    // EU-011's output failure: the mask is validated against the catalogue
    // contract before anything is composed.
    QVector<float> mask;
    if (!readMask(outputs, entry, &mask, &errorId, &message)) return failedResult(errorId, message);

    const xerahs::bg::PipelineOutput composed = xerahs::bg::composeMaskedImage(source, mask, entry);
    if (composed.status != xerahs::bg::PipelineStatus::Ok) return failedResult(composed.errorId(), composed.message());

    xerahs::bg::EngineResult result;
    result.ok = true;
    result.image = composed.image;
    return result;
  }

  void cancel() override {
    m_cancel.store(true);
    QMutexLocker runLock(&m_runMutex);
    if (m_liveRun) m_liveRun->SetTerminate();
  }

  void resetCancel() override { m_cancel.store(false); }

  void close() override {
    QMutexLocker sessionLock(&m_mutex);
    m_session.reset();
    m_openPath.clear();
    m_openDevice = xerahs::bg::DevicePolicy::Auto;
  }

private:
  // Opens (and caches) one model per path+device, cross-checking the
  // session's own contract against the catalogue: the runtime is the second
  // validation gate after the header parse.
  bool openSession(const QString &modelPath, const xerahs::bg::CatalogueEntry &entry,
                   xerahs::bg::DevicePolicy device, QString *errorId, QString *message) {
    if (m_session && m_openPath == modelPath && m_openDevice == device) return true;

    Ort::SessionOptions options;
    if (device == xerahs::bg::DevicePolicy::Gpu) {
      // Only a provider this build actually ships may be requested; a GPU
      // request on a CPU build is the honest typed device failure.
      const std::vector<std::string> providers = Ort::GetAvailableProviders();
      if (providersContain(providers, "CUDAExecutionProvider")) {
        options.AppendExecutionProvider_CUDA(OrtCUDAProviderOptions{});
      } else if (providersContain(providers, "ROCmExecutionProvider")) {
        options.AppendExecutionProvider_ROCM(OrtROCMProviderOptions{});
      } else {
        *errorId = QStringLiteral("model-device-unavailable");
        *message = QStringLiteral("No GPU execution provider is available in this build; "
                                  "background removal runs on the CPU.");
        return false;
      }
      // Note: a shipped provider whose driver libraries fail to load makes
      // onnxruntime fall back to CPU with a stderr warning (the reference
      // tools warn about this); this API version cannot query the active
      // set, so the fallback limitation is recorded here.
    }

    std::unique_ptr<Ort::Session> session;
    try {
      session = std::make_unique<Ort::Session>(m_env, modelPath.toStdString().c_str(), options);
    } catch (const Ort::Exception &) {
      *errorId = QStringLiteral("model-engine-open-failed");
      *message = QStringLiteral("The inference engine refused the model file.");
      return false;
    } catch (const std::bad_alloc &) {
      *errorId = QStringLiteral("model-memory");
      *message = QStringLiteral("Not enough memory to open the model.");
      return false;
    }

    if (!crossCheckContract(*session, entry, errorId, message)) return false;

    m_session = std::move(session);
    m_openPath = modelPath;
    m_openDevice = device;
    return true;
  }

  bool crossCheckContract(const Ort::Session &session, const xerahs::bg::CatalogueEntry &entry,
                          QString *errorId, QString *message) {
    const auto mismatch = [errorId, message](const QString &what) {
      *errorId = QStringLiteral("model-contract-mismatch");
      *message = QStringLiteral("The model's %1 does not match the approved contract.").arg(what);
      return false;
    };
    if (session.GetInputCount() != 1) return mismatch(QStringLiteral("input count"));
    Ort::AllocatorWithDefaultOptions allocator;
    const auto inputName = session.GetInputNameAllocated(0, allocator);
    if (entry.input.name != QString::fromUtf8(inputName.get())) return mismatch(QStringLiteral("input name"));
    if (!shapeMatches(session.GetInputTypeInfo(0), entry.input))
      return mismatch(QStringLiteral("input shape"));
    if (session.GetOutputCount() < 1) return mismatch(QStringLiteral("output count"));
    const auto outputName = session.GetOutputNameAllocated(0, allocator);
    if (entry.firstOutput.name != QString::fromUtf8(outputName.get()))
      return mismatch(QStringLiteral("first output name"));
    if (!shapeMatches(session.GetOutputTypeInfo(0), entry.firstOutput))
      return mismatch(QStringLiteral("first output shape"));
    m_inputName = entry.input.name.toUtf8();
    m_outputName = entry.firstOutput.name.toUtf8();
    return true;
  }

  static bool readMask(const std::vector<Ort::Value> &outputs, const xerahs::bg::CatalogueEntry &entry,
                       QVector<float> *maskOut, QString *errorId, QString *message) {
    const auto invalid = [errorId, message](const QString &what) {
      *errorId = QStringLiteral("model-output-invalid");
      *message = what;
      return false;
    };
    if (outputs.empty()) return invalid(QStringLiteral("The model produced no output."));
    const auto info = outputs.front().GetTensorTypeAndShapeInfo();
    if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
        info.GetShape() != shapeVector(entry.firstOutput))
      return invalid(QStringLiteral("The model output does not match the approved contract."));
    const qint64 count = elementCount(entry.firstOutput);
    const float *data = outputs.front().GetTensorData<float>();
    if (count <= 0 || !data) return invalid(QStringLiteral("The model output is empty."));
    maskOut->resize(qsizetype(count));
    std::memcpy(maskOut->data(), data, std::size_t(count) * sizeof(float));
    return true;
  }

  Ort::Env m_env;
  Ort::MemoryInfo m_memoryInfo{Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)};
  QMutex m_mutex;                             // guards the cached session and names
  std::unique_ptr<Ort::Session> m_session;
  QString m_openPath;
  xerahs::bg::DevicePolicy m_openDevice = xerahs::bg::DevicePolicy::Auto;
  QByteArray m_inputName, m_outputName;
  std::atomic_bool m_cancel{false};
  QMutex m_runMutex;                          // guards the live RunOptions registration
  Ort::RunOptions *m_liveRun = nullptr;
};

}  // namespace xerahs::app

#endif  // XERAHS_ONNXRUNTIME

namespace xerahs::app {

std::unique_ptr<xerahs::bg::MaskEngine> makeOnnxMaskEngine() {
#ifdef XERAHS_ONNXRUNTIME
  return std::make_unique<OnnxMaskEngine>();
#else
  return nullptr;
#endif
}

}  // namespace xerahs::app
