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

QString DesignLibrary::absPath(const QString& rel) const { return m_base + QStringLiteral("/") + rel; }

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
    QFile f(absPath(QStringLiteral("index.json")));
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
        arr.append(o);
    }
    QFile f(absPath(QStringLiteral("index.json")));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    return true;
}

int DesignLibrary::ensurePopulated()
{
    QDir().mkpath(absPath(QStringLiteral("designs")));
    QDir().mkpath(absPath(QStringLiteral("thumbs")));

    const bool fresh = !QFile::exists(absPath(QStringLiteral("index.json")));
    if (fresh) {
        for (const Design& d : DesignFactory::starterSet())
            add(d.name, d.category, d.tags, d.seq, false);
        writeIndex();   // write once after batch loading
    } else {
        readIndex();
    }
    return int(m_entries.size());
}

QString DesignLibrary::add(const QString& name, const QString& category,
                           const QStringList& tags, const StitchSequence& seq,
                           bool updateIndex)
{
    if (m_entries.isEmpty() && QFile::exists(absPath(QStringLiteral("index.json"))))
        readIndex();
    QDir().mkpath(absPath(QStringLiteral("designs")));
    QDir().mkpath(absPath(QStringLiteral("thumbs")));

    const QString id = slug(name) + QStringLiteral("-") +
                       QUuid::createUuid().toString(QUuid::Id128).left(6);
    const QString file  = QStringLiteral("designs/%1.scd").arg(id);
    const QString thumb = QStringLiteral("thumbs/%1.png").arg(id);

    // data
    QFile df(absPath(file));
    if (!df.open(QIODevice::WriteOnly)) return QString();
    df.write(encodeSeq(seq));
    df.close();

    // thumbnail
    StitchThumbnail::render(seq, 240).save(absPath(thumb), "PNG");

    LibraryEntry e;
    e.id = id; e.name = name; e.category = category; e.tags = tags;
    e.file = file; e.thumb = thumb;
    e.stitches = int(seq.realStitchCount()); e.colors = int(seq.palette.size());
    double x0,y0,x1,y1; if (seq.bounds(x0,y0,x1,y1)) { e.wMm = x1-x0; e.hMm = y1-y0; }
    m_entries.push_back(e);
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
    for (const QString& c : DesignFactory::categories())
        for (const LibraryEntry& e : m_entries)
            if (e.category == c) { out << c; break; }
    // include any custom categories not in the built-in list
    for (const LibraryEntry& e : m_entries)
        if (!out.contains(e.category)) out << e.category;
    return out;
}

QVector<LibraryEntry> DesignLibrary::search(const QString& query, const QString& category) const
{
    const QString q = query.trimmed().toLower();
    QVector<LibraryEntry> out;
    for (const LibraryEntry& e : m_entries) {
        if (!category.isEmpty() && e.category != category) continue;
        if (!q.isEmpty()) {
            QString hay = (e.name + " " + e.category + " " + e.tags.join(" ")).toLower();
            if (!hay.contains(q)) continue;
        }
        out.push_back(e);
    }
    return out;
}

} // namespace stick
