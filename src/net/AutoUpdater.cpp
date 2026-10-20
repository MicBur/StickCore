#include "AutoUpdater.h"

#include <QApplication>
#include <QWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QProgressDialog>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QRegularExpression>

namespace stick {

AutoUpdater::AutoUpdater(QWidget* parent)
    : QObject(parent)
    , m_parent(parent)
    , m_silent(true)
{
    QSettings settings(QStringLiteral("StickCore"), QStringLiteral("StickCore"));
    m_token = settings.value(QStringLiteral("Update/GitHubToken"), QString()).toString();
    if (m_token.isEmpty()) {
        m_token = QString::fromLocal8Bit(qgetenv("STICKCORE_GITHUB_TOKEN"));
    }
}

AutoUpdater::~AutoUpdater()
{
    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
    }
}

void AutoUpdater::setGitHubToken(const QString& token)
{
    m_token = token.trimmed();
    QSettings settings(QStringLiteral("StickCore"), QStringLiteral("StickCore"));
    settings.setValue(QStringLiteral("Update/GitHubToken"), m_token);
}

QString AutoUpdater::gitHubToken() const
{
    return m_token;
}

void AutoUpdater::checkForUpdates(bool silentIfUpToDate)
{
    m_silent = silentIfUpToDate;

    QUrl apiUrl(QStringLiteral("https://api.github.com/repos/MicBur/StickCore/releases/latest"));
    QNetworkRequest req(apiUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("StickCore-DesktopApp/1.1.0"));
    req.setRawHeader("Accept", "application/vnd.github.v3+json");

    if (!m_token.isEmpty()) {
        req.setRawHeader("Authorization", QString("token %1").arg(m_token).toUtf8());
    }

    QNetworkReply* reply = m_net.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReleaseReply(reply);
    });
}

void AutoUpdater::onReleaseReply(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (!m_silent) {
            QMessageBox::warning(m_parent,
                QStringLiteral("Update-Prüfung"),
                QStringLiteral("Es konnte nicht nach Updates gesucht werden.<br>Fehler: %1").arg(reply->errorString()));
        }
        emit updateCheckFinished(false, QString());
        return;
    }

    const QByteArray data = reply->readAll();
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (!doc.isObject()) {
        if (!m_silent) {
            QMessageBox::warning(m_parent,
                QStringLiteral("Update-Prüfung"),
                QStringLiteral("Ungültige Antwort vom Update-Server erhalten."));
        }
        emit updateCheckFinished(false, QString());
        return;
    }

    QJsonObject obj = doc.object();
    const QString tagName = obj.value(QStringLiteral("tag_name")).toString();
    m_newVersion = cleanVersion(tagName);
    m_releaseNotes = obj.value(QStringLiteral("body")).toString();

    m_downloadUrl = QUrl(obj.value(QStringLiteral("html_url")).toString());
    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
    for (const auto& a : assets) {
        const QJsonObject ao = a.toObject();
        const QString name = ao.value(QStringLiteral("name")).toString();
        if (name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive) ||
            name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            m_downloadUrl = QUrl(ao.value(QStringLiteral("browser_download_url")).toString());
            break;
        }
    }

    const bool newer = isVersionGreater(m_newVersion, QString::fromLatin1(CURRENT_VERSION));
    emit updateCheckFinished(newer, m_newVersion);

    if (newer) {
        QString notesHtml = m_releaseNotes.toHtmlEscaped();
        notesHtml.replace(QStringLiteral("\n"), QStringLiteral("<br>"));

        QMessageBox box(m_parent);
        box.setWindowTitle(QStringLiteral("StickCore — Update verfügbar"));
        box.setIcon(QMessageBox::Information);
        box.setText(QString(
            "<h3>🚀 Ein neues Update für StickCore ist verfügbar!</h3>"
            "<b>Verfügbare Version:</b> <span style='color:#2dd4bf; font-weight:700;'>v%1</span> "
            "(Ihre Version: v%2)<br><br>"
            "<b>Neuerungen & Verbesserungen:</b><br>"
            "<div style='background:#171a21; color:#dbe0e9; padding:10px; border-radius:8px; max-height:220px; overflow-y:auto; border:1px solid #2c313d;'>"
            "%3"
            "</div><br>"
            "Möchten Sie die offizielle Release-Seite zum Herunterladen öffnen?")
            .arg(m_newVersion)
            .arg(CURRENT_VERSION)
            .arg(notesHtml.isEmpty() ? QStringLiteral("Reguläre System- & Stabilitätsaktualisierung.") : notesHtml));

        QPushButton* btnUpdate = box.addButton(QStringLiteral("Im Browser öffnen"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Später"), QMessageBox::RejectRole);
        box.setDefaultButton(btnUpdate);
        box.exec();

        if (box.clickedButton() == static_cast<QAbstractButton*>(btnUpdate)) {
            const QUrl targetUrl = m_downloadUrl.isValid()
                ? m_downloadUrl
                : QUrl(QStringLiteral("https://github.com/MicBur/StickCore/releases"));
            QDesktopServices::openUrl(targetUrl);
        }
    } else {
        if (!m_silent) {
            QMessageBox::information(m_parent,
                QStringLiteral("StickCore ist aktuell"),
                QStringLiteral("<b>StickCore ist auf dem neuesten Stand!</b><br><br>Installierte Version: <b>v%1</b>")
                    .arg(CURRENT_VERSION));
        }
    }
}

void AutoUpdater::startDownload(const QUrl& url, const QString& /*targetFileName*/)
{
    if (url.isValid()) {
        QDesktopServices::openUrl(url);
    }
}

void AutoUpdater::onDownloadData()
{
}

void AutoUpdater::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    emit downloadProgress(bytesReceived, bytesTotal);
}

void AutoUpdater::onDownloadFinished()
{
    if (m_downloadReply) {
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
}

QString AutoUpdater::cleanVersion(const QString& v)
{
    QString res = v.trimmed();
    if (res.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        res.remove(0, 1);
    }
    return res;
}

bool AutoUpdater::isVersionGreater(const QString& remoteVer, const QString& currentVer)
{
    const QString r = cleanVersion(remoteVer);
    const QString c = cleanVersion(currentVer);

    const auto rParts = r.split(QLatin1Char('.'));
    const auto cParts = c.split(QLatin1Char('.'));

    const int maxLen = std::max(rParts.size(), cParts.size());
    for (int i = 0; i < maxLen; ++i) {
        const int rNum = (i < rParts.size()) ? rParts[i].toInt() : 0;
        const int cNum = (i < cParts.size()) ? cParts[i].toInt() : 0;
        if (rNum > cNum) return true;
        if (rNum < cNum) return false;
    }
    return false;
}

} // namespace stick
