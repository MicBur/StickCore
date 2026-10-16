// ---------------------------------------------------------------------------
//  StickCore  –  LibraryBuilder.cpp
// ---------------------------------------------------------------------------
#include "library/LibraryBuilder.h"
#include "library/DesignLibrary.h"
#include "library/DesignFactory.h"
#include "codec/JefCodec.h"
#include "core/ThreadCatalog.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QDebug>
#include <iostream>

namespace stick {

namespace {

QString sizeToLabel(const QString& s)
{
    const QString l = s.toLower();
    if (l == QStringLiteral("1in"))    return QStringLiteral("1\" (25 mm)");
    if (l == QStringLiteral("1.25in")) return QStringLiteral("1.25\" (32 mm)");
    if (l == QStringLiteral("1.5in") || l == QStringLiteral("1p5in")) return QStringLiteral("1.5\" (38 mm)");
    if (l == QStringLiteral("1.75in")) return QStringLiteral("1.75\" (44 mm)");
    if (l == QStringLiteral("2in"))    return QStringLiteral("2\" (50 mm)");
    if (l == QStringLiteral("2.5in") || l == QStringLiteral("2p5in")) return QStringLiteral("2.5\" (64 mm)");
    if (l == QStringLiteral("3in"))    return QStringLiteral("3\" (76 mm)");
    if (l == QStringLiteral("3.5in") || l == QStringLiteral("3p5in")) return QStringLiteral("3.5\" (89 mm)");
    if (l == QStringLiteral("4in"))    return QStringLiteral("4\" (102 mm)");
    if (l == QStringLiteral("4.5in") || l == QStringLiteral("4p5in")) return QStringLiteral("4.5\" (114 mm)");
    if (l == QStringLiteral("5in"))    return QStringLiteral("5\" (127 mm)");
    if (l == QStringLiteral("5.5in") || l == QStringLiteral("5p5in")) return QStringLiteral("5.5\" (140 mm)");
    if (l == QStringLiteral("6in"))    return QStringLiteral("6\" (152 mm)");
    if (l == QStringLiteral("6.5in") || l == QStringLiteral("6p5in")) return QStringLiteral("6.5\" (165 mm)");
    if (l == QStringLiteral("7in"))    return QStringLiteral("7\" (178 mm)");
    if (l == QStringLiteral("7.5in") || l == QStringLiteral("7p5in")) return QStringLiteral("7.5\" (190 mm)");
    if (l == QStringLiteral("8in"))    return QStringLiteral("8\" (203 mm)");
    if (l == QStringLiteral("8.5in") || l == QStringLiteral("8p5in")) return QStringLiteral("8.5\" (216 mm)");
    if (l == QStringLiteral("9in"))    return QStringLiteral("9\" (228 mm)");
    if (l == QStringLiteral("9.5in") || l == QStringLiteral("9p5in")) return QStringLiteral("9.5\" (241 mm)");
    if (l == QStringLiteral("10in"))   return QStringLiteral("10\" (254 mm)");
    return s;
}

QString normalizeSizeTag(const QString& s)
{
    QString out = s.toLower();
    out.replace(QStringLiteral("p"), QStringLiteral("."));
    return out;
}

} // namespace

QString LibraryBuilder::importJef(DesignLibrary& lib,
                                  const QString& filePath,
                                  const QString& name,
                                  const QString& category,
                                  const QStringList& tags,
                                  const QString& sizeTag,
                                  const QString& explicitId)
{
    StitchSequence seq;
    if (!JefCodec::importFromFile(filePath, seq)) {
        return QString();
    }
    if (seq.stitches.empty()) return QString();
    return lib.add(name, category, tags, seq, false, sizeTag, explicitId);
}

int LibraryBuilder::importFolder(DesignLibrary& lib,
                                 const QString& folderPath,
                                 const QString& category)
{
    QDir dir(folderPath);
    if (!dir.exists()) return 0;

    int imported = 0;
    const QStringList files = dir.entryList({QStringLiteral("*.jef"), QStringLiteral("*.JEF")}, QDir::Files);
    for (const QString& fn : files) {
        const QString p = dir.filePath(fn);
        QFileInfo fi(p);
        const QString name = fi.baseName().replace('_', ' ');
        QStringList tags = { category.toLower(), fi.baseName().toLower() };
        if (!importJef(lib, p, name, category, tags).isEmpty())
            imported++;
    }
    if (imported > 0)
        lib.writeIndex();
    return imported;
}

LibraryBuilder::Stats LibraryBuilder::buildAll(const QString& targetDir,
                                               const QString& mefBaseDir,
                                               const QString& fwDir)
{
    Stats stats;
    DesignLibrary lib(targetDir);

    const QString mef = mefBaseDir.isEmpty() ? QStringLiteral("C:/Users/micbu/Downloads/mef") : mefBaseDir;
    const QString fw  = fwDir.isEmpty()      ? QStringLiteral("E:/StickCore/output")          : fwDir;

    std::cout << "== StickCore LibraryBuilder ==" << std::endl;
    std::cout << "Target library dir: " << targetDir.toStdString() << std::endl;

    // 1. Starter Set
    std::cout << "-> Adding built-in starter designs..." << std::endl;
    for (const Design& d : DesignFactory::starterSet()) {
        lib.add(d.name, d.category, d.tags, d.seq, false);
        stats.starter++;
    }

    // 2. College Outline Letters & Numbers
    const QString collegeDir = mef + QStringLiteral("/College_Outline_part1/JEF");
    if (QDir(collegeDir).exists()) {
        std::cout << "-> Ingesting College Outline Letters & Numbers from: " << collegeDir.toStdString() << std::endl;
        QDir cDir(collegeDir);
        for (const QString& sizeSub : cDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString sizeTag = normalizeSizeTag(sizeSub);
            const QString label   = sizeToLabel(sizeSub);
            QDir subDir(cDir.filePath(sizeSub));
            const QStringList jefs = subDir.entryList({QStringLiteral("*.jef"), QStringLiteral("*.JEF")}, QDir::Files);
            for (const QString& jf : jefs) {
                const QString fullP = subDir.filePath(jf);
                QFileInfo fi(fullP);
                const QString base = fi.baseName();
                const QStringList parts = base.split('_');
                if (parts.isEmpty()) continue;
                const QString ch = parts.first();
                if (ch.length() != 1) continue;

                if (ch[0].isDigit()) {
                    const QString name = QStringLiteral("College Zahl %1 (%2)").arg(ch, label);
                    const QString expId = QStringLiteral("college-zahl-%1-%2").arg(ch, sizeTag);
                    const QStringList tags = {
                        QStringLiteral("college"), QStringLiteral("varsity"),
                        QStringLiteral("zahl"), QStringLiteral("ziffer"),
                        ch, sizeTag, QStringLiteral("nummer")
                    };
                    if (!importJef(lib, fullP, name, QStringLiteral("🔢 College Zahlen (0-9)"),
                                   tags, sizeTag, expId).isEmpty()) {
                        stats.collegeNumbers++;
                    }
                } else if (ch[0].isLetter()) {
                    const QString chUp = ch.toUpper();
                    const QString name = QStringLiteral("College Buchstabe %1 (%2)").arg(chUp, label);
                    const QString expId = QStringLiteral("college-buchstabe-%1-%2").arg(ch.toLower(), sizeTag);
                    const QStringList tags = {
                        QStringLiteral("college"), QStringLiteral("varsity"),
                        QStringLiteral("buchstabe"), chUp, ch.toLower(),
                        sizeTag, QStringLiteral("alphabet"), QStringLiteral("stickschrift")
                    };
                    if (!importJef(lib, fullP, name, QStringLiteral("🏫 College Buchstaben (A-Z)"),
                                   tags, sizeTag, expId).isEmpty()) {
                        stats.collegeLetters++;
                    }
                }
            }
        }
    } else {
        std::cout << "   Warning: College dir not found: " << collegeDir.toStdString() << std::endl;
    }

    // 3. Applique Numbers (0-9)
    const QStringList appBases = {
        mef + QStringLiteral("/01234_applique_numbers"),
        mef + QStringLiteral("/56789_applique_numbers")
    };
    const QRegularExpression rxSize(QStringLiteral("([0-9]+(?:p[0-9]+)?)in"), QRegularExpression::CaseInsensitiveOption);

    std::cout << "-> Ingesting Applique Numbers (0-9)..." << std::endl;
    for (int num = 0; num <= 9; ++num) {
        const QString baseDir = (num <= 4) ? appBases[0] : appBases[1];
        QDir numJefDir(baseDir + QStringLiteral("/%1/JEF").arg(num));
        if (!numJefDir.exists()) continue;

        const QStringList jefs = numJefDir.entryList({QStringLiteral("*.jef"), QStringLiteral("*.JEF")}, QDir::Files);
        for (const QString& jf : jefs) {
            const QString fullP = numJefDir.filePath(jf);
            auto match = rxSize.match(jf);
            QString rawSize = match.hasMatch() ? (match.captured(1).toLower() + QStringLiteral("in")) : QStringLiteral("in");
            const QString sizeTag = normalizeSizeTag(rawSize);
            const QString label = sizeToLabel(rawSize);

            const QString name = QStringLiteral("Applikation Ziffer %1 (%2)").arg(QString::number(num), label);
            const QString expId = QStringLiteral("applique-ziffer-%1-%2").arg(QString::number(num), sizeTag);
            const QStringList tags = {
                QStringLiteral("applikation"), QStringLiteral("applique"),
                QStringLiteral("ziffer"), QStringLiteral("zahl"),
                QString::number(num), sizeTag,
                QStringLiteral("stoffapplikation"), QStringLiteral("satin")
            };
            if (!importJef(lib, fullP, name, QStringLiteral("🪡 Applikation Ziffern (0-9)"),
                           tags, sizeTag, expId).isEmpty()) {
                stats.appliqueNumbers++;
            }
        }
    }

    // 4. Feuerwehr & Kiel Motive
    std::cout << "-> Ingesting Feuerwehr & Kieler Wappen designs..." << std::endl;
    const QString fwSub = fw + QStringLiteral("/fw");
    const QString fwBases = QDir(fwSub).exists() ? fwSub : (fw + QStringLiteral("/EmbF5/MyDesign"));

    auto addFw = [&](const QString& path, const QString& name, const QString& expId, const QStringList& extraTags) {
        if (!QFile::exists(path)) return;
        QStringList tags = { QStringLiteral("feuerwehr"), QStringLiteral("kiel") };
        tags.append(extraTags);
        if (!importJef(lib, path, name, QStringLiteral("🚒 Feuerwehr & Kiel"), tags, QString(), expId).isEmpty()) {
            stats.feuerwehr++;
        }
    };

    addFw(fwBases + QStringLiteral("/BF30_1.JEF"),
          QStringLiteral("Berufsfeuerwehr Kiel 30 Bogen (Teil 1 - Hauptbogen)"),
          QStringLiteral("feuerwehr-kiel-bf30-teil1"),
          { QStringLiteral("bf30"), QStringLiteral("berufsfeuerwehr"), QStringLiteral("bogen"), QStringLiteral("teil1"), QStringLiteral("aufnäher") });

    addFw(fwBases + QStringLiteral("/BF30_2.JEF"),
          QStringLiteral("Berufsfeuerwehr Kiel 30 Bogen (Teil 2 - Schriftzug)"),
          QStringLiteral("feuerwehr-kiel-bf30-teil2"),
          { QStringLiteral("bf30"), QStringLiteral("berufsfeuerwehr"), QStringLiteral("teil2"), QStringLiteral("schrift") });

    addFw(fwBases + QStringLiteral("/BF30_3.JEF"),
          QStringLiteral("Berufsfeuerwehr Kiel 30 Bogen (Teil 3 - Relief & Sterne)"),
          QStringLiteral("feuerwehr-kiel-bf30-teil3"),
          { QStringLiteral("bf30"), QStringLiteral("berufsfeuerwehr"), QStringLiteral("teil3"), QStringLiteral("sterne") });

    addFw(fwBases + QStringLiteral("/BF30_4.JEF"),
          QStringLiteral("Berufsfeuerwehr Kiel 30 Bogen (Teil 4 - Kettelrand)"),
          QStringLiteral("feuerwehr-kiel-bf30-teil4"),
          { QStringLiteral("bf30"), QStringLiteral("berufsfeuerwehr"), QStringLiteral("teil4"), QStringLiteral("kettelrand") });

    // Multi-color composite of BF30 parts 1..4
    {
        StitchSequence compSeq;
        compSeq.palette.push_back(ThreadColor(QColor(0x14, 0x22, 0x3D), QStringLiteral("Marineblau"), 224));
        compSeq.palette.push_back(ThreadColor(QColor(0xFF, 0xFF, 0xFF), QStringLiteral("Weiß"), 2));
        compSeq.palette.push_back(ThreadColor(QColor(0xE6, 0xA8, 0x17), QStringLiteral("Gold"), 250));
        compSeq.palette.push_back(ThreadColor(QColor(0xD3, 0x2F, 0x2F), QStringLiteral("Feuerwehrrot"), 225));

        bool compOk = true;
        for (int p = 1; p <= 4; ++p) {
            const QString partP = fwBases + QStringLiteral("/BF30_%1.JEF").arg(p);
            StitchSequence partSeq;
            if (JefCodec::importFromFile(partP, partSeq) && !partSeq.stitches.empty()) {
                bool first = true;
                for (Stitch st : partSeq.stitches) {
                    if (first) {
                        st.flags = SF_ColorChange;
                        first = false;
                    }
                    st.colorIdx = p - 1;
                    compSeq.stitches.push_back(st);
                }
            } else {
                compOk = false;
            }
        }
        if (compOk && !compSeq.stitches.empty()) {
            lib.add(QStringLiteral("Berufsfeuerwehr Kiel 30 Bogen (Komplett-Emblem 4-Farbig)"),
                    QStringLiteral("🚒 Feuerwehr & Kiel"),
                    { QStringLiteral("feuerwehr"), QStringLiteral("kiel"), QStringLiteral("bf30"),
                      QStringLiteral("berufsfeuerwehr"), QStringLiteral("komplett"), QStringLiteral("emblem"),
                      QStringLiteral("meisterstück"), QStringLiteral("aufnäher") },
                    compSeq, false, QString(), QStringLiteral("feuerwehr-kiel-bf30-komplett"));
            stats.feuerwehr++;
        }
    }

    // Official Kieler Wappen
    addFw(fw + QStringLiteral("/04_Kieler_Wappen_Offiziell_Applique.jef"),
          QStringLiteral("Kieler Wappen (Offizielle Applikation)"),
          QStringLiteral("kieler-wappen-offiziell-applique"),
          { QStringLiteral("wappen"), QStringLiteral("applikation"), QStringLiteral("applique"), QStringLiteral("schiff"), QStringLiteral("nesselblatt") });

    addFw(fw + QStringLiteral("/04_Kieler_Wappen_Offiziell_Kettel_Konturen.jef"),
          QStringLiteral("Kieler Wappen (Kettelrand & Konturen)"),
          QStringLiteral("kieler-wappen-kettel-konturen"),
          { QStringLiteral("wappen"), QStringLiteral("kettelrand"), QStringLiteral("kontur"), QStringLiteral("satin") });

    // Mitte 30 Applique
    addFw(fw + QStringLiteral("/mef_jef/02_MITTE_Zahl_30_Applique_Original_Mef.jef"),
          QStringLiteral("Feuerwehr Jubiläum 30 (Applikation Mitte)"),
          QStringLiteral("feuerwehr-zahl-30-applique-mef"),
          { QStringLiteral("30"), QStringLiteral("zahl"), QStringLiteral("applikation"), QStringLiteral("mef"), QStringLiteral("daniela") });

    // Finalize index
    lib.writeIndex();

    stats.total = stats.starter + stats.collegeLetters + stats.collegeNumbers +
                  stats.appliqueNumbers + stats.feuerwehr;

    std::cout << "== Library Build Summary ==" << std::endl;
    std::cout << "  Starter designs : " << stats.starter << std::endl;
    std::cout << "  College Letters : " << stats.collegeLetters << std::endl;
    std::cout << "  College Numbers : " << stats.collegeNumbers << std::endl;
    std::cout << "  Applique Numbers: " << stats.appliqueNumbers << std::endl;
    std::cout << "  Feuerwehr & Kiel: " << stats.feuerwehr << std::endl;
    std::cout << "  TOTAL DESIGNS   : " << stats.total << std::endl;

    return stats;
}

} // namespace stick
