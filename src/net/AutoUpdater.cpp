#include "AutoUpdater.h"

#include <QApplication>
#include <QCoreApplication>
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
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QRegularExpression>
#include <algorithm>

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
    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
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
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("StickCore-DesktopApp/1.2.0"));
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

    const QUrl htmlReleaseUrl(obj.value(QStringLiteral("html_url")).toString());
    m_downloadUrl = QUrl();
    m_downloadAssetName.clear();

    // Prefer .zip portable asset first so in-app extraction bypasses Windows SmartScreen/Installer blocks
    const QJsonArray assets = obj.value(QStringLiteral("assets")).toArray();
    for (const auto& a : assets) {
        const QJsonObject ao = a.toObject();
        const QString name = ao.value(QStringLiteral("name")).toString();
        if (name.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
            m_downloadUrl = QUrl(ao.value(QStringLiteral("browser_download_url")).toString());
            m_downloadAssetName = name;
            break;
        }
    }
    if (!m_downloadUrl.isValid()) {
        for (const auto& a : assets) {
            const QJsonObject ao = a.toObject();
            const QString name = ao.value(QStringLiteral("name")).toString();
            if (name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
                m_downloadUrl = QUrl(ao.value(QStringLiteral("browser_download_url")).toString());
                m_downloadAssetName = name;
                break;
            }
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
            "Möchten Sie das Update jetzt direkt in StickCore herunterladen und automatisch installieren (ohne Windows-Installationssperre)?")
            .arg(m_newVersion)
            .arg(CURRENT_VERSION)
            .arg(notesHtml.isEmpty() ? QStringLiteral("Reguläre System- & Stabilitätsaktualisierung.") : notesHtml));

        QPushButton* btnAutoUpdate = box.addButton(QStringLiteral("🚀 Jetzt automatisch aktualisieren"), QMessageBox::AcceptRole);
        QPushButton* btnBrowser    = box.addButton(QStringLiteral("🌐 Im Browser öffnen"), QMessageBox::ActionRole);
        box.addButton(QStringLiteral("Später"), QMessageBox::RejectRole);
        box.setDefaultButton(btnAutoUpdate);
        box.exec();

        if (box.clickedButton() == static_cast<QAbstractButton*>(btnAutoUpdate)) {
            if (m_downloadUrl.isValid()) {
                const QString fname = m_downloadAssetName.isEmpty()
                    ? QStringLiteral("StickCore-Update-v%1.zip").arg(m_newVersion)
                    : m_downloadAssetName;
                startDownload(m_downloadUrl, fname);
            } else {
                QDesktopServices::openUrl(htmlReleaseUrl.isValid()
                    ? htmlReleaseUrl
                    : QUrl(QStringLiteral("https://github.com/MicBur/StickCore/releases")));
            }
        } else if (box.clickedButton() == static_cast<QAbstractButton*>(btnBrowser)) {
            QDesktopServices::openUrl(htmlReleaseUrl.isValid()
                ? htmlReleaseUrl
                : QUrl(QStringLiteral("https://github.com/MicBur/StickCore/releases")));
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

void AutoUpdater::startDownload(const QUrl& url, const QString& targetFileName)
{
    if (!url.isValid()) return;

    m_downloadTarget = QDir::temp().filePath(targetFileName);
    if (m_downloadFile) {
        m_downloadFile->close();
        delete m_downloadFile;
    }
    m_downloadFile = new QFile(m_downloadTarget, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(m_parent, QStringLiteral("Update-Fehler"),
            QStringLiteral("Temporäre Update-Datei konnte nicht angelegt werden:\n%1").arg(m_downloadTarget));
        delete m_downloadFile;
        m_downloadFile = nullptr;
        return;
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("StickCore-DesktopApp/1.2.0"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    m_downloadReply = m_net.get(req);
    connect(m_downloadReply, &QNetworkReply::readyRead, this, &AutoUpdater::onDownloadData);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, &AutoUpdater::onDownloadProgress);
    connect(m_downloadReply, &QNetworkReply::finished, this, &AutoUpdater::onDownloadFinished);

    m_progressDialog = new QProgressDialog(
        QStringLiteral("Lade StickCore v%1 herunter (ohne Windows-Sperre)…").arg(m_newVersion),
        QStringLiteral("Abbrechen"), 0, 100, m_parent);
    m_progressDialog->setWindowTitle(QStringLiteral("StickCore Update"));
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setValue(0);
    connect(m_progressDialog, &QProgressDialog::canceled, this, [this]() {
        if (m_downloadReply) m_downloadReply->abort();
    });
    m_progressDialog->show();
}

void AutoUpdater::onDownloadData()
{
    if (m_downloadReply && m_downloadFile) {
        m_downloadFile->write(m_downloadReply->readAll());
    }
}

void AutoUpdater::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    emit downloadProgress(bytesReceived, bytesTotal);
    if (m_progressDialog && bytesTotal > 0) {
        const int pct = static_cast<int>((bytesReceived * 100) / bytesTotal);
        m_progressDialog->setValue(std::clamp(pct, 0, 99));
    }
}

void AutoUpdater::onDownloadFinished()
{
    if (m_progressDialog) {
        m_progressDialog->close();
        m_progressDialog->deleteLater();
        m_progressDialog = nullptr;
    }

    if (!m_downloadReply) return;

    if (m_downloadFile) {
        m_downloadFile->write(m_downloadReply->readAll());
        m_downloadFile->flush();
        m_downloadFile->close();
        delete m_downloadFile;
        m_downloadFile = nullptr;
    }

    const auto err = m_downloadReply->error();
    const QString errStr = m_downloadReply->errorString();
    m_downloadReply->deleteLater();
    m_downloadReply = nullptr;

    if (err != QNetworkReply::NoError) {
        if (err != QNetworkReply::OperationCanceledError) {
            QMessageBox::warning(m_parent, QStringLiteral("Update fehlgeschlagen"),
                QStringLiteral("Das Update konnte nicht heruntergeladen werden:\n%1").arg(errStr));
        }
        return;
    }

    emit updateReadyToInstall(m_downloadTarget);

    const QString appDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const QString zipNative = QDir::toNativeSeparators(m_downloadTarget);
    const QString scriptPath = QDir::toNativeSeparators(QDir::temp().filePath(QStringLiteral("stickcore_apply_update.ps1")));

    QFile scriptFile(scriptPath);
    if (scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        const QString psScript = QStringLiteral(
            "$ErrorActionPreference = 'SilentlyContinue'\r\n"
            "Start-Sleep -Milliseconds 1500\r\n"
            "$targetFile = '%1'\r\n"
            "$appDir = '%2'\r\n"
            "Unblock-File -Path $targetFile\r\n"
            "if ($targetFile.ToLower().EndsWith('.zip')) {\r\n"
            "    $stage = Join-Path $env:TEMP 'StickCoreUpdateStage'\r\n"
            "    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }\r\n"
            "    Expand-Archive -Path $targetFile -DestinationPath $stage -Force\r\n"
            "    Get-ChildItem -Path $stage -Recurse | Unblock-File\r\n"
            "    $exeFound = Get-ChildItem -Path $stage -Filter 'StickCore.exe' -Recurse | Select-Object -First 1\r\n"
            "    if ($exeFound) {\r\n"
            "        $srcDir = $exeFound.Directory.FullName\r\n"
            "        Copy-Item -Path \"$srcDir\\*\" -Destination $appDir -Recurse -Force\r\n"
            "    }\r\n"
            "    Start-Process -FilePath (Join-Path $appDir 'StickCore.exe')\r\n"
            "} else {\r\n"
            "    Start-Process -FilePath $targetFile -ArgumentList '/SILENT' -Wait\r\n"
            "    Start-Process -FilePath (Join-Path $appDir 'StickCore.exe')\r\n"
            "}\r\n"
        ).arg(zipNative, appDir);
        scriptFile.write(psScript.toUtf8());
        scriptFile.close();

        QProcess::startDetached(QStringLiteral("powershell.exe"), {
            QStringLiteral("-NoProfile"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
            QStringLiteral("-File"), scriptPath
        });
        qApp->quit();
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
