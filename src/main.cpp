// ---------------------------------------------------------------------------
//  StickCore  –  main.cpp
// ---------------------------------------------------------------------------
#include "MainWindow.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>
#include <QFontDatabase>
#include <QDir>

int main(int argc, char** argv)
{
    // Request a 3.3 Core context globally before any GL widget is created.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("StickCore"));
    app.setOrganizationName(QStringLiteral("StickCore"));

    // Bundle elegant calligraphy fonts so monograms look identical on every PC.
    const QDir fdir(QStringLiteral(":/fonts"));
    for (const QString& f : fdir.entryList(QStringList() << QStringLiteral("*.ttf")))
        QFontDatabase::addApplicationFont(fdir.filePath(f));
    app.setStyleSheet(stick::MainWindow::styleSheet());
    stick::MainWindow w;
    w.show();

    // Optional: "--shot <file>" grabs the window then quits (for CI/preview).
    const QStringList args = app.arguments();
    const int mi = args.indexOf(QStringLiteral("--manual-shots"));
    if (mi >= 0 && mi + 1 < args.size()) {
        const QString outDir = args[mi + 1];
        QTimer::singleShot(800, [&w, outDir]() {
            w.captureManualScreenshots(outDir);
            qApp->quit();
        });
        return app.exec();
    }

    const int si = args.indexOf(QStringLiteral("--shot"));
    if (si >= 0 && si + 1 < args.size()) {
        const QString out = args[si + 1];
        QTimer::singleShot(900, [&w, out]() {
            // If a dialog is open (e.g. the QR upload dialog), grab that.
            QWidget* target = &w;
            const auto tops = QApplication::topLevelWidgets();
            for (QWidget* tw : tops)
                if (tw->isVisible() && tw->inherits("QDialog")) { target = tw; break; }
            target->grab().save(out);
            qApp->quit();
        });
    }
    return app.exec();
}
