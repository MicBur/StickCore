#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class QWidget;
class QProgressDialog;
class QFile;

namespace stick {

class AutoUpdater : public QObject {
    Q_OBJECT
public:
    static constexpr const char* CURRENT_VERSION = "1.2.0";

    explicit AutoUpdater(QWidget* parent = nullptr);
    ~AutoUpdater() override;

    // Checks GitHub releases API. If silentIfUpToDate is true, only prompts if an update is found.
    void checkForUpdates(bool silentIfUpToDate = true);

    // Set optional GitHub Personal Access Token for private repo release access
    void setGitHubToken(const QString& token);
    QString gitHubToken() const;

signals:
    void updateCheckFinished(bool updateAvailable, const QString& version);
    void downloadProgress(qint64 received, qint64 total);
    void updateReadyToInstall(const QString& installerPath);

private slots:
    void onReleaseReply(QNetworkReply* reply);
    void onDownloadData();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();

private:
    void startDownload(const QUrl& url, const QString& targetFileName);
    static bool isVersionGreater(const QString& remoteVer, const QString& currentVer);
    static QString cleanVersion(const QString& v);

    QWidget* m_parent;
    QNetworkAccessManager m_net;
    bool m_silent;
    QString m_token;
    QString m_newVersion;
    QString m_releaseNotes;
    QUrl m_downloadUrl;
    QString m_downloadAssetName;
    QString m_downloadTarget;

    QNetworkReply* m_downloadReply = nullptr;
    QFile* m_downloadFile = nullptr;
    QProgressDialog* m_progressDialog = nullptr;
};

} // namespace stick
