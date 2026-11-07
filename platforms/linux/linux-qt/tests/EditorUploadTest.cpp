#include "EditorUpload.h"
#include "EditorWindow.h"
#include "EditorCanvas.h"
#include "EditorSource.h"
#include "NativeEditorPreferences.h"
#include "uploads/DestinationCatalog.h"
#include <QAction>
#include <QApplication>
#include <QColorSpace>
#include <QMessageBox>
#include <QLineEdit>
#include <QClipboard>
#include <QFile>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <thread>
using namespace xerahs::app;
using namespace xerahs::editor;
using namespace xerahs::actions;
using namespace xerahs::uploads;
namespace {
LoadedSource fixture() {
  QImage image(40,20,QImage::Format_ARGB32); image.setColorSpace(QColorSpace::SRgb); image.fill(qRgba(255,0,0,128));
  auto source=prepareCapturedSource(image); RectangleAnnotation rect; rect.id=QUuid::createUuid(); rect.start={20,0}; rect.end={40,20};
  rect.style.fillColor=0xFF0000FF; rect.style.strokeColor=0xFF0000FF; rect.style.strokeWidth=1;
  source.document->annotations={rect}; return source;
}
class HttpFixture : public QObject {
public:
  QTcpServer server; QByteArray wire; bool received=false,hold=false; int status=200;
  HttpFixture() {
    connect(&server,&QTcpServer::newConnection,this,[this] {
      auto *peer=server.nextPendingConnection(); connect(peer,&QTcpSocket::disconnected,peer,&QObject::deleteLater);
      connect(peer,&QTcpSocket::readyRead,this,[this,peer] {
        wire+=peer->readAll(); const auto split=wire.indexOf("\r\n\r\n"); if(split<0||received)return;
        QRegularExpression length("Content-Length: (\\d+)",QRegularExpression::CaseInsensitiveOption);
        const auto match=length.match(QString::fromLatin1(wire.left(split)));
        if(!match.hasMatch()||wire.size()<split+4+match.captured(1).toInt())return;
        received=true; if(hold)return;
        const QByteArray body=status==200?QByteArray(R"({"url":"https://example.test/image"})"):QByteArray("fixture failure");
        peer->write("HTTP/1.1 "+QByteArray::number(status)+" Reply\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);peer->disconnectFromHost();
      });
    });
  }
  UploadDestination destination() const {
    UploadDestination d;d.id=QUuid::createUuid();d.name="Fixture upload";d.requestUrl=QString("http://127.0.0.1:%1/upload").arg(server.serverPort());
    d.body=BodyType::MultipartFormData;d.fileFormName="file";d.resultUrl="{json:url}";return d;
  }
};
}
class EditorUploadTest : public QObject {
  Q_OBJECT
  QTemporaryDir settings;
private slots:
  void initTestCase(){QVERIFY(settings.isValid());QSettings::setPath(QSettings::NativeFormat,QSettings::UserScope,settings.path());QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());}
  void init(){QVERIFY(initializeNativeEditorPreferences(settings.filePath("editor.json")).isEmpty());}
  void cleanup(){QThreadPool::globalInstance()->waitForDone();}
  void nativeUpload_data(){
    QTest::addColumn<QString>("mode"); QTest::addColumn<bool>("shortcut");
    for(const auto *mode:{"success","http-error","cancel-confirm","cancel-transfer"}) {
      const bool originalShortcut = QString(mode) == "success";
      QTest::newRow(mode) << QString(mode) << originalShortcut;
      const auto other = QString(mode) + (originalShortcut ? "-menu" : "-shortcut");
      QTest::newRow(other.toUtf8().constData()) << QString(mode) << !originalShortcut;
    }
  }
  void nativeUpload(){
    QFETCH(QString,mode); QFETCH(bool,shortcut); QTemporaryDir dir;HttpFixture http;QVERIFY(http.server.listen(QHostAddress::LocalHost));
    http.status=mode=="http-error"?500:200;http.hold=mode=="cancel-transfer";
    const auto destination=http.destination();DestinationCatalog catalog(dir.filePath("destinations.json"));QVERIFY(catalog.add(destination).isEmpty());
    auto credentials=std::make_shared<MemoryCredentialStore>();auto source=fixture();
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedDocument(*source.document));QVERIFY(window);window->show();
    auto *canvas=window->findChild<EditorCanvas *>();const auto document=serializeDocument(canvas->annotationDocument());const auto state=canvas->annotationState();const auto title=window->windowTitle();
    EditorUploadPorts ports;ports.confirm=[&](const auto &snapshot,QWidget *parent){return chooseEditorUpload(snapshot,parent,dir.filePath("destinations.json"));};
    ports.submit=[credentials](const auto &snapshot,const auto &chosen,const auto &control){return submitEditorUpload(snapshot,chosen,control,credentials);};window->setUploadPorts(ports);
    auto *action=window->findChild<QAction *>("uploadImage");QVERIFY(action&&action->isEnabled());QCOMPARE(action->shortcut(),QKeySequence(Qt::CTRL|Qt::Key_U));
    bool confirmed=false,cancelledTransfer=false;QTimer timer;
    connect(&timer,&QTimer::timeout,this,[&]{
      if(auto *box=window->findChild<QMessageBox *>("confirmEditorUpload");box&&!confirmed){
        confirmed=true;QVERIFY(box->text().contains("Fixture upload"));QVERIFY(box->text().contains("public"));QCOMPARE(box->defaultButton(),box->button(QMessageBox::Cancel));QVERIFY(!action->isEnabled());
        // Consent freezes this destination even if the stored default changes.
        auto replacement=destination;replacement.id=QUuid::createUuid();replacement.requestUrl="http://127.0.0.1:1/elsewhere";
        QVERIFY(catalog.add(replacement).isEmpty());QVERIFY(catalog.setDefault(replacement.id).isEmpty());
        box->button(mode=="cancel-confirm"?QMessageBox::Cancel:QMessageBox::Ok)->click();
      }
      if(mode=="cancel-transfer"&&http.received&&!cancelledTransfer){
        if(auto *dialog=window->findChild<QProgressDialog *>("uploadTransferProgress")){cancelledTransfer=true;dialog->findChild<QPushButton *>()->click();}
      }
    });
    QSignalSpy done(window.get(),&EditorWindow::uploadFinished),saved(window.get(),&EditorWindow::saveFinished);
    if(shortcut){window->activateWindow();canvas->setFocus();QTRY_VERIFY(window->isActiveWindow());}
    timer.start(2);
    if(shortcut)QTest::keyClick(canvas,Qt::Key_U,Qt::ControlModifier);else action->trigger();
    timer.stop();
    QCOMPARE(done.count(),1);QVERIFY(confirmed);QVERIFY(window->isVisible());QCOMPARE(window->windowTitle(),title);QCOMPARE(saved.count(),0);
    QCOMPARE(serializeDocument(canvas->annotationDocument()),document);QCOMPARE(canvas->annotationState(),state);QVERIFY(action->isEnabled());
    if(mode=="cancel-confirm"){QCOMPARE(done.first()[0].toString(),QString("cancelled"));QVERIFY(!http.received);}
    else{
      QVERIFY(http.received);const auto start=http.wire.indexOf("\x89PNG");QVERIFY(start>=0);const auto end=http.wire.indexOf("\r\n--",start);QVERIFY(end>start);
      const auto image=QImage::fromData(http.wire.mid(start,end-start),"PNG");QVERIFY(!image.isNull());QCOMPARE(image.pixel(5,5),qRgba(255,0,0,128));QCOMPARE(image.pixel(30,10),qRgb(0,0,255));
      if(mode=="success"){
        QCOMPARE(done.first()[0].toString(),QString("succeeded"));QCOMPARE(done.first()[2].toString(),QString("https://example.test/image"));
        auto *result=window->findChild<QDialog *>("fileUploadResultDialog");QVERIFY(result&&result->isVisible());
        auto *url=result->findChild<QLineEdit *>("uploadUrl");QVERIFY(url&&url->isReadOnly());QCOMPARE(url->text(),QString("https://example.test/image"));
        result->findChild<QPushButton *>("copyUploadUrl")->click();QCOMPARE(QApplication::clipboard()->text(),url->text());
        QCOMPARE(serializeDocument(canvas->annotationDocument()),document);QCOMPARE(canvas->annotationState(),state);
      }
      if(mode=="http-error"){QCOMPARE(done.first()[0].toString(),QString("failed"));QCOMPARE(done.first()[1].toString(),QString("http-error"));QVERIFY(window->statusBar()->currentMessage().contains("fresh confirmation"));}
      if(mode=="cancel-transfer"){QVERIFY(cancelledTransfer);QCOMPARE(done.first()[0].toString(),QString("unknown"));}
    }
  }
  void missingServiceAndUnsupportedAnnotations(){
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image));QVERIFY(window);EditorUploadPorts ports;ports.submit={};window->setUploadPorts(ports);
    auto *action=window->findChild<QAction *>("uploadImage");QVERIFY(!action->isEnabled());QVERIFY(action->toolTip().contains("no upload service"));
    auto source=fixture();auto id=QUuid::createUuid();source.document->annotations.emplace_back(UnsupportedAnnotation{id,{{"id",id.toString(QUuid::WithoutBraces)},{"type","future-shape"},{"visible",true}}});
    int confirmations=0;ports={};ports.confirm=[&](const auto &,QWidget *){++confirmations;return EditorUploadChoice{};};QWidget parent;
    const auto result=runEditorUpload(source.image,*source.document,&parent,ports);QCOMPARE(result.action.diagnostic,std::optional<QString>{"upload-annotation-unsupported"});QCOMPARE(confirmations,0);
  }
  void missingDestinationExplainsConfigurationWithoutSubmitting(){
    QTemporaryDir dir;QVERIFY(dir.isValid());
    std::unique_ptr<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image));QVERIFY(window);
    EditorUploadPorts ports;int submissions=0;
    ports.confirm=[&](const auto &snapshot,QWidget *parent){return chooseEditorUpload(snapshot,parent,dir.filePath("absent.json"));};
    ports.submit=[&](const auto &,const auto &,const auto &){++submissions;return ActionOutcome{};};window->setUploadPorts(ports);
    QSignalSpy done(window.get(),&EditorWindow::uploadFinished);window->findChild<QAction *>("uploadImage")->trigger();
    QCOMPARE(submissions,0);QCOMPARE(done.count(),1);QCOMPARE(done.first()[1].toString(),QString("upload-destination-missing"));
    QVERIFY(window->statusBar()->currentMessage().contains("Upload destinations"));QVERIFY(!QFile::exists(dir.filePath("absent.json")));
  }
  void destroyingEditorDuringRenderNeverConfirmsOrUploads(){
    auto entered=std::make_shared<std::atomic<bool>>(false),released=std::make_shared<std::atomic<bool>>(false);int confirmations=0;
    QPointer<EditorWindow> window(EditorWindow::fromCapturedImage(fixture().image));QVERIFY(window);EditorUploadPorts ports;
    ports.beforeRender=[entered,released]{entered->store(true);while(!released->load())std::this_thread::sleep_for(std::chrono::milliseconds(1));};
    ports.confirm=[&](const auto &,QWidget *){++confirmations;return EditorUploadChoice{};};window->setUploadPorts(ports);
    QTimer timer;connect(&timer,&QTimer::timeout,this,[&]{if(entered->load()&&window)delete window.data();});timer.start(2);
    window->findChild<QAction *>("uploadImage")->trigger();timer.stop();released->store(true);QVERIFY(!window);QCOMPARE(confirmations,0);
  }
};
QTEST_MAIN(EditorUploadTest)
#include "EditorUploadTest.moc"
