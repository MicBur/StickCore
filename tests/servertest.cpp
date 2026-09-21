// Integration test: start the upload server, POST an image via curl, confirm
// the image is received and decoded. Simulates the phone upload over HTTP.
#include "net/QrUploadServer.h"
#include <QCoreApplication>
#include <QTimer>
#include <QProcess>
#include <QImage>
#include <cstdio>

#include <QDir>
#include <QFile>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString imgPath = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : (QDir::tempPath() + QStringLiteral("/stickcore_testmotif.png"));
    if (argc <= 1 || !QFile::exists(imgPath)) {
        QImage dummy(64, 64, QImage::Format_RGB32);
        dummy.fill(QColor(220, 50, 50));
        dummy.save(imgPath, "PNG");
    }

    stick::QrUploadServer srv;
    if (!srv.start(8091)) { std::printf("FAIL: listen\n"); return 2; }
    std::printf("server listening, url=%s\n", qPrintable(srv.url()));

    int rc = 1;
    QObject::connect(&srv, &stick::QrUploadServer::imageReceived, [&](const QImage& im){
        std::printf("RECEIVED image %dx%d\n", im.width(), im.height());
        rc = (im.width() > 0 && im.height() > 0) ? 0 : 3;
        QCoreApplication::quit();
    });

    QTimer::singleShot(400, [&]{
        auto* p = new QProcess(&app);
        p->start("curl", {"-s","-F",
            QStringLiteral("image=@%1;type=image/png").arg(imgPath),
            "http://127.0.0.1:8091/upload"});
    });
    QTimer::singleShot(6000, [&]{ std::printf("FAIL: timeout\n"); rc = 1; QCoreApplication::quit(); });

    app.exec();
    std::printf(rc==0 ? "ALL PASSED\n" : "FAILED\n");
    return rc;
}
