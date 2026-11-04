#include "NativeAccessibility.h"
#include "VideoConverterDialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QUrl>
#include <QVBoxLayout>

namespace xerahs::app {
using namespace media;

VideoConverterDialog::VideoConverterDialog(ConversionRunner *runner, const ElementAvailability *availability, QWidget *parent)
    : QDialog(parent), m_runner(runner), m_availability(availability) {
  new NativeAccessibility(*this, tr("Choose input, output, container and codec. Convert starts the operation; Cancel or Close stops it."), true);
  setWindowTitle(tr("Video converter"));
  setObjectName(QStringLiteral("videoConverterDialog"));
  setAcceptDrops(true);
  m_runner->setParent(this);
  m_openFolder = [](const QString &folder) { return QDesktopServices::openUrl(QUrl::fromLocalFile(folder)); };

  auto make = [this](auto *w, const char *name, const QString &accessible) { w->setObjectName(name); w->setAccessibleName(accessible); return w; };
  m_input = make(new QLineEdit(this), "vcInput", tr("Input video"));
  m_output = make(new QLineEdit(this), "vcOutput", tr("Output file"));
  m_container = make(new QComboBox(this), "vcContainer", tr("Container"));
  m_codec = make(new QComboBox(this), "vcCodec", tr("Codec"));
  m_bitrate = make(new QSpinBox(this), "vcBitrate", tr("Bitrate in kilobits per second"));
  m_bitrate->setRange(0, 200000); m_bitrate->setSuffix(tr(" kbit/s")); m_bitrate->setSpecialValueText(tr("Encoder default"));
  m_audio = make(new QCheckBox(tr("Keep &audio"), this), "vcAudio", tr("Keep audio")); m_audio->setChecked(true);
  m_overwrite = make(new QCheckBox(tr("&Replace the output if it exists"), this), "vcOverwrite", tr("Replace existing output"));
  m_chooseInput = make(new QPushButton(tr("&Browse…"), this), "vcChooseInput", tr("Browse for input video"));
  m_chooseOutput = make(new QPushButton(tr("B&rowse…"), this), "vcChooseOutput", tr("Browse for output file"));
  m_convert = make(new QPushButton(tr("&Convert"), this), "vcConvert", tr("Convert"));
  m_cancel = make(new QPushButton(tr("Ca&ncel conversion"), this), "vcCancel", tr("Cancel conversion"));
  m_show = make(new QPushButton(tr("&Show in folder"), this), "vcShow", tr("Show in folder"));
  m_close = make(new QPushButton(tr("C&lose"), this), "vcClose", tr("Close"));
  m_progress = make(new QProgressBar(this), "vcProgress", tr("Conversion progress")); m_progress->setRange(0, 100);
  m_status = make(new QLabel(tr("Choose or drop a video, then pick a container and codec."), this), "vcStatus", tr("Conversion status"));
  m_status->setTextFormat(Qt::PlainText); m_status->setWordWrap(true);
  m_status->setAccessibleDescription(tr("Describes the current conversion state or error"));
  m_input->setPlaceholderText(tr("Drop a video file here")); m_show->setVisible(false);

  for (auto c : containers()) {
    bool any = false;
    for (const auto &codec : codecs()) any = any || codec.containers.contains(c);
    if (any) m_container->addItem(containerLabel(c), containerId(c));
  }
  auto *inputRow = new QHBoxLayout; inputRow->addWidget(m_input, 1); inputRow->addWidget(m_chooseInput);
  auto *outputRow = new QHBoxLayout; outputRow->addWidget(m_output, 1); outputRow->addWidget(m_chooseOutput);
  auto *form = new QFormLayout;
  form->addRow(tr("&Input:"), inputRow); form->addRow(tr("Co&ntainer:"), m_container);
  form->addRow(tr("C&odec:"), m_codec); form->addRow(tr("&Bitrate:"), m_bitrate);
  form->addRow(QString(), m_audio); form->addRow(tr("&Output:"), outputRow); form->addRow(QString(), m_overwrite);
  auto *buttons = new QHBoxLayout; buttons->addWidget(m_convert); buttons->addWidget(m_cancel); buttons->addWidget(m_show); buttons->addStretch(); buttons->addWidget(m_close);
  auto *layout = new QVBoxLayout(this); layout->addLayout(form); layout->addLayout(buttons); layout->addWidget(m_progress); layout->addWidget(m_status);
  setTabOrder(m_input, m_chooseInput); setTabOrder(m_chooseInput, m_container); setTabOrder(m_container, m_codec);
  setTabOrder(m_codec, m_bitrate); setTabOrder(m_bitrate, m_audio); setTabOrder(m_audio, m_output); setTabOrder(m_output, m_chooseOutput);
  setTabOrder(m_chooseOutput, m_overwrite); setTabOrder(m_overwrite, m_convert); setTabOrder(m_convert, m_cancel);
  resize(640, 360);

  connect(m_chooseInput, &QPushButton::clicked, this, &VideoConverterDialog::chooseInput);
  connect(m_chooseOutput, &QPushButton::clicked, this, &VideoConverterDialog::chooseOutput);
  connect(m_convert, &QPushButton::clicked, this, &VideoConverterDialog::convert);
  connect(m_cancel, &QPushButton::clicked, this, &VideoConverterDialog::cancel);
  connect(m_show, &QPushButton::clicked, this, &VideoConverterDialog::showInFolder);
  connect(m_close, &QPushButton::clicked, this, &QDialog::close);
  connect(m_input, &QLineEdit::editingFinished, this, [this] { setInput(m_input->text()); });
  connect(m_output, &QLineEdit::textEdited, this, [this] { m_outputEdited = true; });
  connect(m_container, &QComboBox::currentIndexChanged, this, [this] {
    fillCodecs();
    if (!m_outputEdited && !m_input->text().isEmpty()) { Container c; if (containerFromId(m_container->currentData().toString(), &c)) m_output->setText(suggestOutputPath(m_input->text(), c)); }
  });
  connect(m_codec, &QComboBox::currentIndexChanged, this, [this] { updateControls(); });
  connect(m_runner, &ConversionRunner::progress, m_progress, &QProgressBar::setValue);
  connect(m_runner, &ConversionRunner::finished, this, &VideoConverterDialog::onFinished);
  connect(this, &QDialog::finished, this, [this] { if (busy()) m_runner->cancel(); });   // Esc/reject
  fillCodecs();
}

QString VideoConverterDialog::input() const { return m_input->text(); }
QString VideoConverterDialog::output() const { return m_output->text(); }
QString VideoConverterDialog::statusText() const { return m_status->text(); }
bool VideoConverterDialog::busy() const { return m_runner->busy(); }

// EU-012/EU-013: codecs for the chosen container; unusable ones stay listed but disabled with a reason.
void VideoConverterDialog::fillCodecs() {
  Container container;
  if (!containerFromId(m_container->currentData().toString(), &container)) return;
  const auto previous = m_codec->currentData().toString();
  QSignalBlocker block(m_codec);
  m_codec->clear();
  int firstUsable = -1, keep = -1;
  for (const auto &codec : codecs()) {
    if (!codec.containers.contains(container)) continue;
    const auto availability = codecAvailability(codec, container, *m_availability);
    // The codec label comes from the catalog; only the availability suffix is prose.
    m_codec->addItem(availability.usable ? codec.label : codec.label + tr(" (unavailable)"), codec.id);
    const int row = m_codec->count() - 1;
    if (!availability.usable) {
      m_codec->setItemData(row, availability.reason, Qt::ToolTipRole);
      m_codec->setItemData(row, availability.reason, Qt::AccessibleDescriptionRole);
      if (auto *item = qobject_cast<QStandardItemModel *>(m_codec->model())->item(row)) item->setEnabled(false);
    } else {
      if (firstUsable < 0) firstUsable = row;
      if (codec.id == previous) keep = row;
    }
  }
  m_codec->setCurrentIndex(keep >= 0 ? keep : firstUsable >= 0 ? firstUsable : 0);
  updateControls();
}

void VideoConverterDialog::updateControls() {
  const bool running = busy();
  const auto *codec = findCodec(m_codec->currentData().toString());
  const bool hasBitrate = codec && !codec->bitrateProperty.isEmpty();
  Container container = Container::Mp4; containerFromId(m_container->currentData().toString(), &container);
  m_bitrate->setEnabled(!running && hasBitrate);
  m_audio->setEnabled(!running && containerAllowsAudio(container));
  for (auto *w : std::initializer_list<QWidget *>{m_input, m_output, m_container, m_codec, m_overwrite, m_chooseInput, m_chooseOutput}) w->setEnabled(!running);
  m_convert->setEnabled(!running); m_cancel->setEnabled(running);
  m_progress->setEnabled(true);
}

void VideoConverterDialog::setInput(const QString &path) {
  if (busy() || path.trimmed().isEmpty()) return;
  m_input->setText(path); m_show->setVisible(false); m_progress->setValue(0);
  if (!m_outputEdited) { Container c; if (containerFromId(m_container->currentData().toString(), &c)) m_output->setText(suggestOutputPath(path, c)); }
  m_status->setText(tr("Ready to convert %1.").arg(QFileInfo(path).fileName()));
}

void VideoConverterDialog::chooseInput() {
  if (busy()) return;
  // The filter lists file patterns, not prose: it stays as it is.
  const auto path = QFileDialog::getOpenFileName(this, tr("Choose a video to convert"), {}, QStringLiteral("Video files (*.mp4 *.mkv *.webm *.avi *.mov *.gif *.m4v *.mpg *.mpeg *.wmv *.flv *.ogv);;All files (*)"));
  if (!path.isEmpty()) setInput(path);
}

void VideoConverterDialog::chooseOutput() {
  if (busy()) return;
  const auto path = QFileDialog::getSaveFileName(this, tr("Choose the output file"), m_output->text());
  if (!path.isEmpty()) { m_output->setText(path); m_outputEdited = true; }
}

void VideoConverterDialog::convert() {
  if (busy()) return;
  Request request; request.inputPath = m_input->text(); request.outputPath = m_output->text(); request.codecId = m_codec->currentData().toString();
  containerFromId(m_container->currentData().toString(), &request.container);
  request.bitrateKbps = m_bitrate->isEnabled() ? m_bitrate->value() : 0;
  request.includeAudio = m_audio->isEnabled() && m_audio->isChecked(); request.overwrite = m_overwrite->isChecked();
  // The runner re-validates with the probed input; this catches everything knowable up front.
  const auto plan = planConversion(request, {}, *m_availability);
  if (!plan.ok()) { m_status->setText(tr("%1 (%2)").arg(plan.message, errorCodeId(plan.error))); return; }
  m_show->setVisible(false); m_progress->setValue(0); m_lastOutput.clear();
  m_status->setText(tr("Converting…"));
  m_runner->start(request);
  updateControls();
}

void VideoConverterDialog::cancel() {
  if (!busy()) return;
  m_status->setText(tr("Cancelling…"));
  m_runner->cancel();
}

void VideoConverterDialog::onFinished(const ConversionResult &result) {
  QString text;
  switch (result.outcome) {
    case Outcome::Success: text = tr("Saved %1.").arg(QFileInfo(result.outputPath).fileName()); m_progress->setValue(100); m_lastOutput = result.outputPath; break;
    case Outcome::Cancelled: text = tr("Cancelled. No output was kept."); m_progress->setValue(0); break;
    case Outcome::Failed: text = tr("Conversion failed (%1): %2").arg(errorCodeId(result.error), result.message); break;
  }
  for (const auto &d : result.diagnostics) text += '\n' + d;
  m_status->setText(text);
  m_show->setVisible(!m_lastOutput.isEmpty());
  updateControls();
}

void VideoConverterDialog::showInFolder() {
  if (m_lastOutput.isEmpty()) return;
  if (!m_openFolder(QFileInfo(m_lastOutput).absolutePath())) m_status->setText(tr("No file manager could show this folder."));
}

void VideoConverterDialog::dragEnterEvent(QDragEnterEvent *event) {
  if (!busy() && event->mimeData()->hasUrls()) event->acceptProposedAction(); else event->ignore();
}
void VideoConverterDialog::dropEvent(QDropEvent *event) {
  for (const auto &url : event->mimeData()->urls()) if (url.isLocalFile()) { setInput(url.toLocalFile()); event->acceptProposedAction(); return; }
  event->ignore();
}

// EU-017: closing a busy window cancels the run and removes its partial output.
void VideoConverterDialog::closeEvent(QCloseEvent *event) {
  if (busy()) m_runner->cancel();
  QDialog::closeEvent(event);
}

}  // namespace xerahs::app
