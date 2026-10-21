// ---------------------------------------------------------------------------
//  StickCore  –  DesignLibrary.cpp
// ---------------------------------------------------------------------------
#include "library/DesignLibrary.h"
#include "library/DesignFactory.h"
#include "library/StitchThumbnail.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDataStream>
#include <QUuid>
#include <QImage>
#include <QCoreApplication>

namespace stick {

namespace {

QByteArray encodeSeq(const StitchSequence& s)
{
    QByteArray raw;
    QDataStream ds(&raw, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << quint32(s.palette.size());
    for (const ThreadColor& t : s.palette) {
        ds << quint8(t.color.red()) << quint8(t.color.green()) << quint8(t.color.blue())
           << qint32(t.janomeCode);
        const QByteArray n = t.description.toUtf8();
        ds << quint16(n.size()); ds.writeRawData(n.constData(), n.size());
    }
    ds << quint32(s.stitches.size());
    for (const Stitch& st : s.stitches) {
        ds << qint32(std::llround(st.x*100.0)) << qint32(std::llround(st.y*100.0))
           << quint16(st.flags) << quint16(st.colorIdx);
    }
    return qCompress(raw);
}

bool decodeSeq(const QByteArray& blob, StitchSequence& out)
{
    const QByteArray raw = qUncompress(blob);
    if (raw.isEmpty()) return false;
    QDataStream ds(raw);
    ds.setByteOrder(QDataStream::LittleEndian);
    out.clear();
    quint32 pc = 0; ds >> pc;
    for (quint32 i = 0; i < pc; ++i) {
        quint8 r,g,b; qint32 code; ds >> r >> g >> b >> code;
        quint16 nl = 0; ds >> nl; QByteArray n(nl, 0); ds.readRawData(n.data(), nl);
        ThreadColor tc(QColor(r,g,b), QString::fromUtf8(n), code);
        out.palette.push_back(tc);
    }
    quint32 sc = 0; ds >> sc;
    out.stitches.reserve(sc);
    for (quint32 i = 0; i < sc; ++i) {
        qint32 x,y; quint16 fl,ci; ds >> x >> y >> fl >> ci;
        out.stitches.emplace_back(x/100.0, y/100.0, fl, ci);
    }
    return true;
}

} // namespace

DesignLibrary::DesignLibrary(const QString& baseDir)
{
    m_base = baseDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/library")
        : baseDir;
}

QString DesignLibrary::bundledPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/library");
}

QString DesignLibrary::absPath(const QString& rel) const
{
    const QString userPath = m_base + QStringLiteral("/") + rel;
    if (QFile::exists(userPath)) return userPath;
    const QString bPath = bundledPath() + QStringLiteral("/") + rel;
    if (QFile::exists(bPath)) return bPath;
    return userPath;
}

QString DesignLibrary::slug(const QString& s)
{
    QString o;
    for (QChar c : s.toLower()) {
        if (c.isLetterOrNumber()) o += c;
        else if (!o.isEmpty() && o.back() != '-') o += '-';
    }
    while (o.endsWith('-')) o.chop(1);
    if (o.isEmpty()) o = QStringLiteral("design");
    return o;
}

bool DesignLibrary::readIndex()
{
    m_entries.clear();
    QString idxPath = absPath(QStringLiteral("index.json"));
    if (!QFile::exists(idxPath)) {
        idxPath = bundledPath() + QStringLiteral("/index.json");
    }
    QFile f(idxPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        LibraryEntry e;
        e.id = o["id"].toString(); e.name = o["name"].toString();
        e.category = o["category"].toString();
        for (const QJsonValue& t : o["tags"].toArray()) e.tags << t.toString();
        e.file = o["file"].toString(); e.thumb = o["thumb"].toString();
        e.stitches = o["stitches"].toInt(); e.colors = o["colors"].toInt();
        e.wMm = o["w"].toDouble(); e.hMm = o["h"].toDouble();
        e.sizeTag = o["sizeTag"].toString();
        if (e.sizeTag.isEmpty()) {
            for (const QString& t : e.tags) {
                if (t.endsWith(QStringLiteral("in")) || t.endsWith(QStringLiteral("mm"))) {
                    e.sizeTag = t;
                    break;
                }
            }
        }
        m_entries.push_back(e);
    }
    return true;
}

bool DesignLibrary::writeIndex() const
{
    QJsonArray arr;
    for (const LibraryEntry& e : m_entries) {
        QJsonObject o;
        o["id"] = e.id; o["name"] = e.name; o["category"] = e.category;
        QJsonArray tg; for (const QString& t : e.tags) tg.append(t); o["tags"] = tg;
        o["file"] = e.file; o["thumb"] = e.thumb;
        o["stitches"] = e.stitches; o["colors"] = e.colors;
        o["w"] = e.wMm; o["h"] = e.hMm;
        if (!e.sizeTag.isEmpty()) o["sizeTag"] = e.sizeTag;
        arr.append(o);
    }
    QDir().mkpath(m_base);
    QFile f(m_base + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

int DesignLibrary::syncFromBundled(const QString& bundledDir)
{
    const QString bIndex = bundledDir + QStringLiteral("/index.json");
    if (!QFile::exists(bIndex)) return 0;

    QFile f(bIndex);
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QJsonArray bArr = QJsonDocument::fromJson(f.readAll()).array();
    f.close();

    QDir().mkpath(m_base + QStringLiteral("/designs"));
    QDir().mkpath(m_base + QStringLiteral("/thumbs"));

    int changed = 0;
    for (const QJsonValue& v : bArr) {
        const QJsonObject o = v.toObject();
        const QString id = o["id"].toString();
        if (id.isEmpty()) continue;

        LibraryEntry e;
        e.id = id; e.name = o["name"].toString();
        e.category = o["category"].toString();
        for (const QJsonValue& t : o["tags"].toArray()) e.tags << t.toString();
        e.file = o["file"].toString(); e.thumb = o["thumb"].toString();
        e.stitches = o["stitches"].toInt(); e.colors = o["colors"].toInt();
        e.wMm = o["w"].toDouble(); e.hMm = o["h"].toDouble();
        e.sizeTag = o["sizeTag"].toString();

        const QString srcFile  = bundledDir + QStringLiteral("/") + e.file;
        const QString dstFile  = m_base + QStringLiteral("/") + e.file;
        const QString srcThumb = bundledDir + QStringLiteral("/") + e.thumb;
        const QString dstThumb = m_base + QStringLiteral("/") + e.thumb;

        const bool isPriority = e.category.contains(QStringLiteral("Feuerwehr"));

        if (QFile::exists(srcFile)) {
            const bool needCopy = !QFile::exists(dstFile) || isPriority ||
                                  (QFileInfo(srcFile).size() != QFileInfo(dstFile).size());
            if (needCopy) {
                QDir().mkpath(QFileInfo(dstFile).dir().path());
                if (QFile::exists(dstFile)) QFile::remove(dstFile);
                QFile::copy(srcFile, dstFile);
            }
        }
        if (QFile::exists(srcThumb)) {
            const bool needCopy = !QFile::exists(dstThumb) || isPriority ||
                                  (QFileInfo(srcThumb).size() != QFileInfo(dstThumb).size());
            if (needCopy) {
                QDir().mkpath(QFileInfo(dstThumb).dir().path());
                if (QFile::exists(dstThumb)) QFile::remove(dstThumb);
                QFile::copy(srcThumb, dstThumb);
            }
        }

        bool found = false;
        for (LibraryEntry& ex : m_entries) {
            if (ex.id == id) {
                if (ex.name != e.name || ex.stitches != e.stitches || ex.category != e.category || isPriority) {
                    ex = e;
                    changed++;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            m_entries.push_back(e);
            changed++;
        }
    }

    // Remove obsolete composite entry if present
    for (int i = int(m_entries.size()) - 1; i >= 0; --i) {
        if (m_entries[i].id == QStringLiteral("feuerwehr-kiel-bf30-komplett")) {
            m_entries.removeAt(i);
            changed++;
        }
    }

    // Sort m_entries so "🚒 Feuerwehr & Kiel" is ALWAYS at the very top of the library
    QVector<LibraryEntry> fwEntries;
    QVector<LibraryEntry> otherEntries;
    for (const LibraryEntry& e : m_entries) {
        if (e.category.contains(QStringLiteral("Feuerwehr"))) {
            fwEntries.push_back(e);
        } else {
            otherEntries.push_back(e);
        }
    }
    if (!fwEntries.isEmpty()) {
        // Order fwEntries according to bundled index priority order
        QStringList priorityOrder = {
            QStringLiteral("feuerwehr-kiel-wwwap-wappen"),
            QStringLiteral("feuerwehr-zahl-30-applique-mef"),
            QStringLiteral("feuerwehr-kiel-bf30-all"),
            QStringLiteral("feuerwehr-kiel-nw30-all"),
            QStringLiteral("feuerwehr-kiel-nw2-all"),
            QStringLiteral("feuerwehr-kiel-bf30-teil1"),
            QStringLiteral("feuerwehr-kiel-bf30-teil2"),
            QStringLiteral("feuerwehr-kiel-bf30-teil3"),
            QStringLiteral("feuerwehr-kiel-bf30-teil4"),
            QStringLiteral("feuerwehr-kiel-nw30-teil1"),
            QStringLiteral("feuerwehr-kiel-nw30-teil2"),
            QStringLiteral("feuerwehr-kiel-nw30-teil3"),
            QStringLiteral("feuerwehr-kiel-nw2-teil1"),
            QStringLiteral("feuerwehr-kiel-nw2-teil2"),
            QStringLiteral("feuerwehr-kiel-nw2-teil3"),
            QStringLiteral("kieler-wappen-offiziell-applique"),
            QStringLiteral("kieler-wappen-kettel-konturen")
        };
        std::sort(fwEntries.begin(), fwEntries.end(), [&](const LibraryEntry& a, const LibraryEntry& b) {
            int ia = priorityOrder.indexOf(a.id);
            int ib = priorityOrder.indexOf(b.id);
            if (ia < 0) ia = 999;
            if (ib < 0) ib = 999;
            return ia < ib;
        });

        QVector<LibraryEntry> combined = fwEntries;
        combined.append(otherEntries);
        m_entries = combined;
        changed++;
    }

    if (changed > 0) {
        writeIndex();
    }
    return changed;
}

int DesignLibrary::ensurePopulated()
{
    QDir().mkpath(absPath(QStringLiteral("designs")));
    QDir().mkpath(absPath(QStringLiteral("thumbs")));

    const QString bDir = bundledPath();
    const bool haveBundled = QFile::exists(bDir + QStringLiteral("/index.json"));
    const bool haveUserIndex = QFile::exists(m_base + QStringLiteral("/index.json"));

    if (haveUserIndex) {
        readIndex();
        if (haveBundled) {
            syncFromBundled(bDir);
        }
    } else if (haveBundled) {
        syncFromBundled(bDir);
        readIndex();
    } else {
        // Fresh start without bundled files: generate built-in starter set
        for (const Design& d : DesignFactory::starterSet())
            add(d.name, d.category, d.tags, d.seq, false);
        writeIndex();
    }
    return int(m_entries.size());
}

QString DesignLibrary::add(const QString& name, const QString& category,
                           const QStringList& tags, const StitchSequence& seq,
                           bool updateIndex, const QString& sizeTag,
                           const QString& explicitId)
{
    if (m_entries.isEmpty() && QFile::exists(absPath(QStringLiteral("index.json"))))
        readIndex();
    QDir().mkpath(m_base + QStringLiteral("/designs"));
    QDir().mkpath(m_base + QStringLiteral("/thumbs"));

    const QString id = !explicitId.isEmpty() ? explicitId : (slug(name) + QStringLiteral("-") +
                       QUuid::createUuid().toString(QUuid::Id128).left(6));
    const QString file  = QStringLiteral("designs/%1.scd").arg(id);
    const QString thumb = QStringLiteral("thumbs/%1.png").arg(id);

    // data
    QFile df(m_base + QStringLiteral("/") + file);
    if (!df.open(QIODevice::WriteOnly)) return QString();
    df.write(encodeSeq(seq));
    df.close();

    // thumbnail (realistic thread on fabric)
    StitchThumbnail::render(seq, 240, QColor(0xEF, 0xEA, 0xDE), true).save(m_base + QStringLiteral("/") + thumb, "PNG");

    LibraryEntry e;
    e.id = id; e.name = name; e.category = category; e.tags = tags;
    e.file = file; e.thumb = thumb;
    e.stitches = int(seq.realStitchCount()); e.colors = int(seq.palette.size());
    e.sizeTag = sizeTag;
    double x0,y0,x1,y1; if (seq.bounds(x0,y0,x1,y1)) { e.wMm = x1-x0; e.hMm = y1-y0; }

    // Replace if same id exists, else append
    bool found = false;
    for (LibraryEntry& existing : m_entries) {
        if (existing.id == id) {
            existing = e;
            found = true;
            break;
        }
    }
    if (!found) m_entries.push_back(e);

    if (updateIndex)
        writeIndex();
    return id;
}

bool DesignLibrary::load(const LibraryEntry& e, StitchSequence& out) const
{
    QFile f(absPath(e.file));
    if (!f.open(QIODevice::ReadOnly)) return false;
    return decodeSeq(f.readAll(), out);
}

QStringList DesignLibrary::categoriesPresent() const
{
    QStringList out;
    // Put Feuerwehr & Kiel first if present
    for (const LibraryEntry& e : m_entries) {
        if (e.category.contains(QStringLiteral("Feuerwehr")) && !out.contains(e.category)) {
            out << e.category;
        }
    }
    for (const QString& c : DesignFactory::categories()) {
        for (const LibraryEntry& e : m_entries) {
            if (e.category == c && !out.contains(c)) { out << c; break; }
        }
    }
    // include any custom categories not in the built-in list
    for (const LibraryEntry& e : m_entries) {
        if (!out.contains(e.category)) out << e.category;
    }
    return out;
}

QVector<LibraryEntry> DesignLibrary::search(const QString& query,
                                           const QString& category,
                                           const QString& sizeTag) const
{
    const QString q = query.trimmed().toLower();
    const QString st = sizeTag.trimmed().toLower();
    QVector<LibraryEntry> out;
    for (const LibraryEntry& e : m_entries) {
        if (!category.isEmpty() && e.category != category) continue;
        if (!st.isEmpty() && e.sizeTag.toLower() != st && !e.tags.contains(st)) continue;
        if (!q.isEmpty()) {
            QString hay = (e.name + " " + e.category + " " + e.tags.join(" ")).toLower();
            if (!hay.contains(q)) continue;
        }
        out.push_back(e);
    }
    return out;
}

} // namespace stick
