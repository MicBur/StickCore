// ---------------------------------------------------------------------------
//  StickCore  –  main.cpp
// ---------------------------------------------------------------------------
#include "MainWindow.h"
#include "library/LibraryBuilder.h"
#include "library/LibraryDialog.h"

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

    const QStringList args = app.arguments();
    const int bi = args.indexOf(QStringLiteral("--build-library"));
    if (bi >= 0) {
        QString targetDir = (bi + 1 < args.size() && !args[bi + 1].startsWith(QStringLiteral("--")))
            ? args[bi + 1]
            : (QDir::currentPath() + QStringLiteral("/library"));
        QString mefDir = (bi + 2 < args.size() && !args[bi + 2].startsWith(QStringLiteral("--")))
            ? args[bi + 2]
            : QStringLiteral("C:/Users/micbu/Downloads/mef");
        stick::LibraryBuilder::buildAll(targetDir, mefDir);
        return 0;
    }

    stick::MainWindow w;
    w.show();
    const int mi = args.indexOf(QStringLiteral("--manual-shots"));
    if (mi >= 0 && mi + 1 < args.size()) {
        const QString outDir = args[mi + 1];
        QTimer::singleShot(800, [&w, outDir]() {
            w.captureManualScreenshots(outDir);
            qApp->quit();
        });
        return app.exec();
    }

    const int libi = args.indexOf(QStringLiteral("--shot-library"));
    if (libi >= 0 && libi + 1 < args.size()) {
        const QString out = args[libi + 1];
        const QString cat = (libi + 2 < args.size() && !args[libi + 2].startsWith(QStringLiteral("--")))
            ? args[libi + 2] : QString();
        QTimer::singleShot(500, [&w, out, cat]() {
            auto* dlg = new stick::LibraryDialog(w.library(), cat, &w);
            dlg->show();
            qApp->processEvents();
            QTimer::singleShot(250, [dlg, out]() {
                dlg->grab().save(out);
                qApp->quit();
            });
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
