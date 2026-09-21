// ---------------------------------------------------------------------------
//  StickCore  –  QrUploadServer.cpp
// ---------------------------------------------------------------------------
#include "net/QrUploadServer.h"

#include <QTcpSocket>
#include <QNetworkInterface>
#include <QHostAddress>

namespace stick {

static const char* kPage = R"HTML(<!DOCTYPE html><html lang="de"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>StickCore Studio — Mobile Upload</title>
<style>
* { box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
       background: #12141a; color: #dbe0e9; margin: 0; padding: 20px;
       display: flex; min-height: 100vh; align-items: center; justify-content: center; }
.card { background: #1b1e26; border: 1px solid #2c313d; border-radius: 18px;
        padding: 26px; max-width: 380px; width: 100%; box-shadow: 0 8px 32px rgba(0,0,0,0.4); text-align: center; }
.logo { font-size: 32px; margin-bottom: 6px; }
h1 { font-size: 22px; font-weight: 700; margin: 0 0 8px; color: #f1f5f9; }
p { color: #8b93a4; font-size: 14px; line-height: 1.5; margin: 0 0 22px; }
.btn-grid { display: flex; flex-direction: column; gap: 12px; margin-bottom: 16px; }
.btn { width: 100%; padding: 15px 18px; border: 0; border-radius: 12px;
       font-size: 15px; font-weight: 600; cursor: pointer; display: flex;
       align-items: center; justify-content: center; gap: 10px; transition: all 0.2s ease; }
.btn-primary { background: #2dd4bf; color: #04201c; }
.btn-primary:active { background: #14b8a6; transform: scale(0.98); }
.btn-secondary { background: #232732; color: #dbe0e9; border: 1px solid #3b4252; }
.btn-secondary:active { background: #2b3040; transform: scale(0.98); }
.btn-upload { background: #3b82f6; color: #ffffff; font-size: 16px; padding: 16px; }
.btn-upload:active { background: #2563eb; transform: scale(0.98); }
#previewBox { display: none; margin: 18px 0; padding: 12px; background: #14171f;
              border-radius: 12px; border: 1px solid #2c313d; }
#previewImg { max-width: 100%; max-height: 220px; border-radius: 8px; object-fit: contain; }
#fileInfo { margin-top: 8px; font-size: 12px; color: #2dd4bf; word-break: break-all; font-weight: 500; }
.status-msg { display: none; margin-top: 14px; font-size: 14px; color: #2dd4bf; font-weight: 600; }
</style>
</head>
<body>
<div class="card">
  <div class="logo">🧵</div>
  <h1>StickCore Studio</h1>
  <p>Wähle ein Bild oder eine Grafik aus deiner Fotomediathek / Speicher oder nimm ein Foto auf.</p>

  <form id="upForm" method="post" action="/upload" enctype="multipart/form-data">
    <!-- Regular file picker: no capture attribute, allows gallery, downloads, cloud storage -->
    <input type="file" id="fileInput" name="image" accept="image/*,.png,.jpg,.jpeg,.bmp,.webp" style="display:none">
    <!-- Camera-only picker for direct camera capture -->
    <input type="file" id="camInput" accept="image/*" capture="environment" style="display:none">

    <div class="btn-grid">
      <button type="button" class="btn btn-primary" onclick="document.getElementById('fileInput').click()">
        <span>📁</span><span>Foto / Datei aus Mediathek</span>
      </button>
      <button type="button" class="btn btn-secondary" onclick="document.getElementById('camInput').click()">
        <span>📸</span><span>Neues Foto aufnehmen</span>
      </button>
    </div>

    <div id="previewBox">
      <img id="previewImg" alt="Vorschau">
      <div id="fileInfo"></div>
    </div>

    <button type="submit" id="submitBtn" class="btn btn-upload" style="display:none">
      <span>⚡</span><span>An StickCore senden</span>
    </button>
    <div id="statusMsg" class="status-msg">Wird übertragen… Bitte warten.</div>
  </form>
</div>

<script>
const fileIn = document.getElementById('fileInput');
const camIn  = document.getElementById('camInput');
const form   = document.getElementById('upForm');
const pBox   = document.getElementById('previewBox');
const pImg   = document.getElementById('previewImg');
const pInfo  = document.getElementById('fileInfo');
const subBtn = document.getElementById('submitBtn');
const sMsg   = document.getElementById('statusMsg');

function handleFile(file) {
  if (!file) return;
  const reader = new FileReader();
  reader.onload = e => {
    pImg.src = e.target.result;
    pBox.style.display = 'block';
    pInfo.textContent = file.name + ' (' + Math.round(file.size / 1024) + ' KB)';
    subBtn.style.display = 'flex';
  };
  reader.readAsDataURL(file);
}

fileIn.addEventListener('change', e => {
  if (e.target.files && e.target.files[0]) handleFile(e.target.files[0]);
});

camIn.addEventListener('change', e => {
  if (e.target.files && e.target.files[0]) {
    // Copy camera file to form input
    const dt = new DataTransfer();
    dt.items.add(e.target.files[0]);
    fileIn.files = dt.files;
    handleFile(e.target.files[0]);
  }
});

form.addEventListener('submit', () => {
  subBtn.style.display = 'none';
  sMsg.style.display = 'block';
});
</script>
</body></html>)HTML";

static const char* kDone = R"HTML(<!DOCTYPE html><html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Erfolgreich empfangen</title>
<style>body{font-family:-apple-system,sans-serif;background:#12141a;color:#dbe0e9;display:flex;
min-height:100vh;align-items:center;justify-content:center;text-align:center;padding:20px}
.c{max-width:340px;background:#1b1e26;border:1px solid #2c313d;border-radius:18px;padding:32px}
.big{font-size:52px;margin-bottom:12px;color:#2dd4bf}
h2{margin:0 0 10px;font-size:20px;color:#f1f5f9}
p{color:#8b93a4;font-size:14px;line-height:1.5;margin:0 0 24px}
a{display:inline-block;padding:12px 20px;background:#232732;color:#2dd4bf;border:1px solid #2dd4bf;
border-radius:10px;text-decoration:none;font-weight:600}
a:hover{background:#2dd4bf;color:#04201c}</style></head><body><div class="c">
<div class="big">✓</div><h2>Bild empfangen!</h2>
<p>StickCore Studio hat das Bild erhalten und öffnet den Digitalisierungs-Assistenten. Du kannst weitere Dateien senden.</p>
<a href="/">Weitere Datei senden</a></div></body></html>)HTML";

QrUploadServer::QrUploadServer(QObject* parent) : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &QrUploadServer::onNewConnection);
}

bool QrUploadServer::start(quint16 port)
{
    if (m_server.isListening()) return true;
    if (!m_server.listen(QHostAddress::Any, port)) return false;
    m_port = m_server.serverPort();
    return true;
}

void QrUploadServer::stop() { m_server.close(); }

QString QrUploadServer::url() const
{
    const QString ip = localAddress();
    return QStringLiteral("http://%1:%2/").arg(ip.isEmpty() ? QStringLiteral("127.0.0.1") : ip)
                                          .arg(m_port);
}

QString QrUploadServer::localAddress()
{
    QString fallback;
    for (const QNetworkInterface& ifc : QNetworkInterface::allInterfaces()) {
        const auto flags = ifc.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)) continue;
        if (flags & QNetworkInterface::IsLoopBack) continue;
        for (const QNetworkAddressEntry& e : ifc.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol) continue;
            const QString s = a.toString();
            if (s.startsWith("192.168.") || s.startsWith("10.") || s.startsWith("172."))
                return s;                 // most likely the LAN address
            if (fallback.isEmpty()) fallback = s;
        }
    }
    return fallback;
}

void QrUploadServer::onNewConnection()
{
    while (QTcpSocket* s = m_server.nextPendingConnection()) {
        m_buffers.insert(s, QByteArray());
        connect(s, &QTcpSocket::readyRead, this, &QrUploadServer::onReadyRead);
        connect(s, &QTcpSocket::disconnected, this, [this, s]{
            m_buffers.remove(s);
            s->deleteLater();
        });
    }
}

void QrUploadServer::onReadyRead()
{
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;
    QByteArray& buf = m_buffers[sock];
    buf += sock->readAll();

    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;                       // headers incomplete

    // For POST, wait until the full body has arrived.
    const QByteArray head = buf.left(headerEnd);
    int contentLength = 0;
    for (const QByteArray& line : head.split('\n')) {
        const QByteArray l = line.trimmed().toLower();
        if (l.startsWith("content-length:"))
            contentLength = l.mid(15).trimmed().toInt();
    }
    if (head.left(4).toUpper().startsWith("POST")) {
        if (buf.size() < headerEnd + 4 + contentLength) return;   // more to come
    }
    handleRequest(sock, buf, headerEnd);
}

void QrUploadServer::handleRequest(QTcpSocket* sock, const QByteArray& buf, int headerEnd)
{
    const QByteArray head = buf.left(headerEnd);
    const QByteArray reqLine = head.left(head.indexOf('\n')).trimmed();
    const bool isPost = reqLine.toUpper().startsWith("POST");

    auto respond = [&](const char* status, const QByteArray& body){
        QByteArray r = "HTTP/1.1 ";
        r += status; r += "\r\nContent-Type: text/html; charset=utf-8\r\n";
        r += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        r += "Connection: close\r\n\r\n";
        r += body;
        sock->write(r);
        sock->disconnectFromHost();
    };

    if (isPost) {
        QByteArray boundary;
        for (const QByteArray& line : head.split('\n')) {
            const QByteArray l = line.trimmed();
            const int bi = l.toLower().indexOf("boundary=");
            if (bi >= 0) { boundary = l.mid(bi + 9); if (boundary.startsWith('"')) boundary = boundary.mid(1, boundary.size()-2); }
        }
        const QByteArray body = buf.mid(headerEnd + 4);
        const QByteArray img  = parseMultipartImage(body, boundary);
        QImage image;
        if (!img.isEmpty()) image.loadFromData(img);
        if (!image.isNull()) {
            respond("200 OK", kDone);
            emit imageReceived(image);
        } else {
            respond("400 Bad Request", "<h3>Kein gültiges Bild.</h3>");
        }
    } else {
        respond("200 OK", kPage);
    }
}

QByteArray QrUploadServer::parseMultipartImage(const QByteArray& body, const QByteArray& boundary)
{
    if (boundary.isEmpty()) return {};
    const QByteArray delim = "--" + boundary;
    int pos = body.indexOf(delim);
    while (pos >= 0) {
        const int start = pos + delim.size();
        const int next  = body.indexOf(delim, start);
        if (next < 0) break;
        const QByteArray part = body.mid(start, next - start);
        const int hdrEnd = part.indexOf("\r\n\r\n");
        if (hdrEnd >= 0) {
            const QByteArray head = part.left(hdrEnd).toLower();
            if (head.contains("filename=") || head.contains("content-type: image")) {
                QByteArray payload = part.mid(hdrEnd + 4);
                if (payload.endsWith("\r\n")) payload.chop(2);
                return payload;
            }
        }
        pos = next;
    }
    return {};
}

} // namespace stick
