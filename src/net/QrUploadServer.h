// ---------------------------------------------------------------------------
//  StickCore  –  QrUploadServer.h
//
//  A tiny embedded HTTP server so a phone on the same Wi-Fi can upload a photo
//  to StickCore. It serves a mobile upload page (GET /) and receives the image
//  (POST /upload, multipart/form-data). No cloud, no account — LAN only.
// ---------------------------------------------------------------------------
#pragma once

#include <QObject>
#include <QTcpServer>
#include <QHash>
#include <QByteArray>
#include <QImage>

namespace stick {

class QrUploadServer : public QObject {
    Q_OBJECT
public:
    explicit QrUploadServer(QObject* parent = nullptr);

    bool    start(quint16 port = 8080);   ///< listen on all interfaces
    void    stop();
    QString url() const;                  ///< http://<lan-ip>:<port>/
    quint16 port() const { return m_port; }
    bool    isListening() const { return m_server.isListening(); }

    /// Best-guess LAN IPv4 address of this machine ("" if none).
    static QString localAddress();

    /// Extract the uploaded file's bytes from a multipart/form-data body.
    /// Public and pure so it can be unit-tested without a socket.
    static QByteArray parseMultipartImage(const QByteArray& body,
                                          const QByteArray& boundary);

signals:
    void imageReceived(const QImage& image);

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    void handleRequest(class QTcpSocket* sock, const QByteArray& buf, int headerEnd);

    // Declaration order matters: m_server is destroyed FIRST (it deletes its
    // child sockets, whose cleanup touches m_buffers), so m_buffers must
    // outlive it — hence m_server is declared last.
    quint16    m_port = 0;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QTcpServer m_server;
};

} // namespace stick
