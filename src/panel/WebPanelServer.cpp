#include "panel/WebPanelServer.h"
#include "app/ApplicationState.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QTcpSocket>
#include <QUrlQuery>
#include <algorithm>

namespace rtsp {
namespace {
QString generateToken() {
    const auto* chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    QString token; token.reserve(32);
    for (int i = 0; i < 32; ++i) token.append(chars[QRandomGenerator::global()->bounded(62)]);
    return token;
}
// Single-file mobile-friendly control page. Deliberately minimal: status
// polling + start/stop/delay/logs, not a rebuild of the desktop UI.
const char* panelHtmlTemplate() {
    return R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Relay Panel</title>
<style>
:root{color-scheme:dark;}
body{background:#14171c;color:#e8eaed;font-family:-apple-system,Segoe UI,Roboto,sans-serif;margin:0;padding:16px;}
h1{font-size:18px;margin:0 0 14px;}
.card{background:#1b1f26;border:1px solid #2a2f38;border-radius:10px;padding:14px;margin-bottom:12px;}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:8px;}
.name{font-weight:600;font-size:15px;}
.state{font-size:12px;color:#9199a6;}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:6px;}
.dot.on{background:#2fb170;}.dot.off{background:#565c66;}.dot.err{background:#e5484d;}
button{background:#262b33;border:1px solid #363c47;color:#e8eaed;border-radius:6px;padding:8px 12px;font-size:13px;font-weight:600;}
button.primary{background:#3979d9;border:0;}
button.danger{background:transparent;border:1px solid #6e3038;color:#e5828a;}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:6px;font-size:12px;color:#9199a6;margin:8px 0;}
.grid b{color:#e8eaed;font-weight:600;font-family:monospace;}
.delayrow{display:flex;gap:6px;flex-wrap:wrap;margin-top:8px;}
.delayrow button{flex:1;min-width:44px;}
#gate{position:fixed;inset:0;background:#14171c;display:flex;align-items:center;justify-content:center;flex-direction:column;gap:10px;padding:20px;}
#gate input{background:#1b1f26;border:1px solid #363c47;color:#e8eaed;border-radius:6px;padding:10px;width:100%;max-width:320px;box-sizing:border-box;}
#logbox{background:#0f1114;border-radius:6px;padding:8px;font-family:monospace;font-size:11px;max-height:180px;overflow-y:auto;white-space:pre-wrap;}
#app{display:none;}
</style></head>
<body>
<div id="gate">
  <div style="font-size:15px;">Enter panel token</div>
  <input id="tokenInput" type="text" placeholder="token">
  <button class="primary" onclick="saveToken()">Connect</button>
</div>
<div id="app">
<h1>Relay Panel</h1>
<div id="profiles"></div>
<div class="card">
  <div class="name" style="margin-bottom:6px;">Logs</div>
  <div id="logbox"></div>
</div>
</div>
<script>
let TOKEN = localStorage.getItem('panelToken') || '';
const params = new URLSearchParams(location.search);
if (params.get('token')) { TOKEN = params.get('token'); localStorage.setItem('panelToken', TOKEN); }

function saveToken() {
  TOKEN = document.getElementById('tokenInput').value.trim();
  localStorage.setItem('panelToken', TOKEN);
  boot();
}
async function api(path, opts) {
  opts = opts || {};
  opts.headers = Object.assign({}, opts.headers, {'Authorization': 'Bearer ' + TOKEN});
  const res = await fetch(path, opts);
  if (res.status === 401) { document.getElementById('gate').style.display='flex'; document.getElementById('app').style.display='none'; throw new Error('unauthorized'); }
  return res.json();
}
function dotClass(s) {
  if (s.state === 'Error') return 'err';
  if (s.destinationConnected || s.ingestRunning) return 'on';
  return 'off';
}
function fmt(n, unit) { return (Math.round(n * 10) / 10) + unit; }
function render(data) {
  const container = document.getElementById('profiles');
  container.innerHTML = '';
  data.profiles.forEach(p => {
    const card = document.createElement('div'); card.className = 'card';
    const running = !!p.ingestRunning || !!p.destinationConnected;
    card.innerHTML = `
      <div class="row">
        <div><span class="dot ${dotClass(p)}"></span><span class="name">${p.name}</span></div>
        <div class="state">${p.state || 'Unknown'}</div>
      </div>
      <div class="grid">
        <div>Source: <b>${p.sourceConnected ? 'Connected' : 'Waiting'}</b></div>
        <div>Destination: <b>${p.destinationConnected ? 'Connected' : 'Stopped'}</b></div>
        <div>Effective delay: <b>${fmt(p.effectiveDelaySeconds||0,'s')}</b></div>
        <div>Requested delay: <b>${fmt(p.requestedDelaySeconds||0,'s')}</b></div>
        <div>Buffer: <b>${fmt(p.bufferSeconds||0,'s')}</b></div>
        <div>Uptime: <b>${p.uptimeSeconds||0}s</b></div>
      </div>
      <div class="row">
        <button class="primary" data-act="start" ${running?'disabled':''}>Start</button>
        <button class="danger" data-act="stop" ${running?'':'disabled'}>Stop</button>
      </div>
      <div class="delayrow">
        <button data-delay="-10">-10s</button>
        <button data-delay="-1">-1s</button>
        <button data-delay="1">+1s</button>
        <button data-delay="10">+10s</button>
      </div>`;
    card.querySelector('[data-act="start"]').onclick = () => api(`/api/profiles/${p.id}/start`, {method:'POST'}).then(load);
    card.querySelector('[data-act="stop"]').onclick = () => api(`/api/profiles/${p.id}/stop`, {method:'POST'}).then(load);
    card.querySelectorAll('[data-delay]').forEach(btn => {
      btn.onclick = () => {
        const delta = parseInt(btn.getAttribute('data-delay'), 10);
        const next = Math.max(0, Math.round(p.requestedDelaySeconds||0) + delta);
        api(`/api/profiles/${p.id}/delay`, {method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({seconds: next})}).then(load);
      };
    });
    container.appendChild(card);
  });
}
let logSince = 0;
async function loadLogs() {
  try {
    const data = await api(`/api/logs?since=${logSince}`);
    const box = document.getElementById('logbox');
    data.lines.forEach(l => { box.textContent += `[${l.timestamp}] ${l.component}: ${l.message}\n`; });
    logSince = data.nextSince;
    box.scrollTop = box.scrollHeight;
  } catch (e) {}
}
async function load() {
  try {
    const data = await api('/api/status');
    document.getElementById('gate').style.display='none';
    document.getElementById('app').style.display='block';
    render(data);
  } catch (e) {}
}
function boot() { load(); loadLogs(); }
if (TOKEN) boot(); else document.getElementById('gate').style.display='flex';
setInterval(() => { if (TOKEN) { load(); loadLogs(); } }, 2000);
</script>
</body></html>)HTML";
}
}

WebPanelServer::WebPanelServer(ApplicationController& controller, QObject* parent)
    : QObject(parent), controller_(controller) {
    QSettings settings;
    settings.beginGroup("WebPanel");
    token_ = settings.value("token").toString();
    if (token_.isEmpty()) { token_ = generateToken(); settings.setValue("token", token_); }
    settings.endGroup();
    connect(&server_, &QTcpServer::newConnection, this, &WebPanelServer::onNewConnection);
    connect(controller_.logger(), &Logger::entry, this, &WebPanelServer::recordLogLine);
}

bool WebPanelServer::start(quint16 requestedPort) {
    if (server_.isListening()) return true;
    QSettings settings; settings.beginGroup("WebPanel");
    const quint16 port = requestedPort ? requestedPort : static_cast<quint16>(settings.value("port", 8787).toInt());
    settings.setValue("port", port); settings.endGroup();
    if (!server_.listen(QHostAddress::Any, port)) {
        controller_.logger()->log(Severity::Error, "WebPanel",
            QString("Failed to start on port %1: %2").arg(port).arg(server_.errorString()));
        return false;
    }
    controller_.logger()->log(Severity::Info, "WebPanel",
        QString("Remote panel listening on port %1. Open %2 on your phone (same tailnet/VPN).")
            .arg(port).arg(suggestedUrl()));
    return true;
}

void WebPanelServer::stop() {
    for (auto* socket : pending_.keys()) socket->disconnectFromHost();
    pending_.clear();
    server_.close();
}

bool WebPanelServer::isRunning() const { return server_.isListening(); }
quint16 WebPanelServer::port() const { return server_.serverPort(); }

QString WebPanelServer::regenerateToken() {
    token_ = generateToken();
    QSettings settings; settings.beginGroup("WebPanel"); settings.setValue("token", token_); settings.endGroup();
    return token_;
}

QString WebPanelServer::suggestedUrl() const {
    QString host;
    for (const auto& iface : QNetworkInterface::allAddresses()) {
        if (iface.protocol() != QAbstractSocket::IPv4Protocol) continue;
        if (iface.isLoopback()) continue;
        host = iface.toString(); break;
    }
    if (host.isEmpty()) host = "127.0.0.1";
    return QString("http://%1:%2/?token=%3").arg(host).arg(server_.serverPort()).arg(token_);
}

void WebPanelServer::onNewConnection() {
    while (server_.hasPendingConnections()) {
        QTcpSocket* socket = server_.nextPendingConnection();
        pending_.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]{ onSocketReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]{ pending_.remove(socket); socket->deleteLater(); });
    }
}

void WebPanelServer::onSocketReadyRead(QTcpSocket* socket) {
    auto it = pending_.find(socket);
    if (it == pending_.end()) return;
    it->buffer += socket->readAll();
    const int headerEnd = it->buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        if (it->buffer.size() > 64 * 1024) socket->disconnectFromHost();
        return;
    }
    const QByteArray headerBlock = it->buffer.left(headerEnd);
    const QList<QByteArray> lines = headerBlock.split('\n');
    if (lines.isEmpty()) { socket->disconnectFromHost(); return; }
    const QStringList requestParts = QString::fromUtf8(lines[0]).trimmed().split(' ', Qt::SkipEmptyParts);
    if (requestParts.size() < 2) { sendResponse(socket, 400, "Bad request", "text/plain"); socket->disconnectFromHost(); return; }
    QHash<QString, QString> headers;
    for (int i = 1; i < lines.size(); ++i) {
        const QString line = QString::fromUtf8(lines[i]).trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0) headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }
    const int contentLength = headers.value("content-length").toInt();
    const int bodyStart = headerEnd + 4;
    if (it->buffer.size() < bodyStart + contentLength) return; // wait for the rest of the body
    const QByteArray body = it->buffer.mid(bodyStart, contentLength);
    const QString method = requestParts[0].toUpper();
    const QString path = requestParts[1];
    pending_.remove(socket);
    handleRequest(socket, method, path, headers, body);
}

void WebPanelServer::handleRequest(QTcpSocket* socket, const QString& method, const QString& path,
                                    const QHash<QString, QString>& headers, const QByteArray& body) {
    route(socket, method, path, headers, body);
}

bool WebPanelServer::authorized(const QHash<QString, QString>& headers) const {
    const QString value = headers.value("authorization");
    return value == QStringLiteral("Bearer %1").arg(token_);
}

QJsonObject WebPanelServer::profileStatusJson(const AppConfig& profile) const {
    QJsonObject o;
    o["id"] = profile.profileId;
    o["name"] = profile.profileName;
    o["color"] = profile.profileColor;
    o["requestedDelaySeconds"] = profile.requestedDelaySeconds;
    return o;
}

void WebPanelServer::route(QTcpSocket* socket, const QString& method, const QString& rawPath,
                            const QHash<QString, QString>& headers, const QByteArray& body) {
    QString path = rawPath; QString query;
    const int queryPos = path.indexOf('?');
    if (queryPos >= 0) { query = path.mid(queryPos + 1); path = path.left(queryPos); }

    if (method == "GET" && path == "/") {
        sendResponse(socket, 200, QByteArray(panelHtmlTemplate()), "text/html; charset=utf-8");
        return;
    }
    if (!authorized(headers)) { sendJsonError(socket, 401, "Missing or invalid bearer token."); return; }

    if (method == "GET" && path == "/api/status") {
        QHash<QString, AppSnapshot> byId;
        for (const auto& snap : controller_.sessionSnapshots()) byId.insert(snap.profileId, snap);
        QJsonArray arr;
        for (const auto& profile : controller_.profiles()) {
            QJsonObject o = profileStatusJson(profile);
            if (byId.contains(profile.profileId)) {
                const auto& s = byId.value(profile.profileId);
                o["state"] = toString(s.state);
                o["ingestRunning"] = s.ingestRunning;
                o["sourceConnected"] = s.sourceConnected;
                o["destinationConnected"] = s.destinationConnected;
                o["effectiveDelaySeconds"] = s.effectiveDelayUs / 1000000.0;
                o["bufferSeconds"] = s.bufferDurationUs / 1000000.0;
                o["bufferMiB"] = s.bufferBytes / (1024.0 * 1024.0);
                o["uptimeSeconds"] = s.uptimeSeconds;
                o["activeEncoder"] = s.activeEncoder;
            }
            arr.append(o);
        }
        QJsonObject root; root["profiles"] = arr; root["selectedProfileId"] = controller_.selectedProfileId();
        sendJson(socket, 200, QJsonDocument(root));
        return;
    }
    if (method == "GET" && path == "/api/logs") {
        int since = 0;
        const auto items = QUrlQuery(query).queryItems();
        for (const auto& item : items) if (item.first == "since") since = item.second.toInt();
        QJsonArray arr; int index = 0;
        for (const auto& line : logBuffer_) {
            if (index >= since) {
                QJsonObject o; o["index"] = index; o["timestamp"] = line.timestamp;
                o["severity"] = line.severity; o["component"] = line.component; o["message"] = line.message;
                arr.append(o);
            }
            ++index;
        }
        QJsonObject root; root["lines"] = arr; root["nextSince"] = index;
        sendJson(socket, 200, QJsonDocument(root));
        return;
    }
    static const QRegularExpression profileAction(R"(^/api/profiles/([^/]+)/(start|stop|delay|cancel-delay-increase)$)");
    if (method == "POST") {
        const auto match = profileAction.match(path);
        if (match.hasMatch()) {
            const QString profileId = match.captured(1);
            const QString action = match.captured(2);
            const bool profileExists = std::any_of(controller_.profiles().begin(), controller_.profiles().end(),
                [&](const AppConfig& p) { return p.profileId == profileId; });
            if (!profileExists) { sendJsonError(socket, 404, "No such profile."); return; }
            if (action == "start") { controller_.startProfile(profileId); sendJson(socket, 200, QJsonDocument(QJsonObject{{"ok", true}})); return; }
            if (action == "stop") { controller_.stopProfile(profileId); sendJson(socket, 200, QJsonDocument(QJsonObject{{"ok", true}})); return; }
            if (action == "cancel-delay-increase") {
                const bool cancelled = controller_.cancelDelayIncreaseForProfile(profileId);
                sendJson(socket, 200, QJsonDocument(QJsonObject{{"ok", cancelled}}));
                return;
            }
            if (action == "delay") {
                const auto doc = QJsonDocument::fromJson(body);
                if (!doc.isObject() || !doc.object().contains("seconds")) {
                    sendJsonError(socket, 400, "Expected a JSON body: {\"seconds\": N}."); return;
                }
                controller_.applyDelayForProfile(profileId, doc.object().value("seconds").toInt(0));
                sendJson(socket, 200, QJsonDocument(QJsonObject{{"ok", true}}));
                return;
            }
        }
    }
    sendJsonError(socket, 404, "Not found.");
}

void WebPanelServer::sendResponse(QTcpSocket* socket, int status, const QByteArray& body, const char* contentType) {
    const char* statusText = status == 200 ? "OK" : status == 204 ? "No Content" : status == 400 ? "Bad Request"
        : status == 401 ? "Unauthorized" : status == 404 ? "Not Found" : "Internal Server Error";
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + statusText + "\r\n";
    response += QByteArray("Content-Type: ") + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void WebPanelServer::sendJson(QTcpSocket* socket, int status, const QJsonDocument& doc) {
    sendResponse(socket, status, doc.toJson(QJsonDocument::Compact), "application/json");
}

void WebPanelServer::sendJsonError(QTcpSocket* socket, int status, const QString& message) {
    QJsonObject o; o["error"] = message;
    sendJson(socket, status, QJsonDocument(o));
}

void WebPanelServer::recordLogLine(QString timestamp, int severity, QString component, QString message) {
    logBuffer_.push_back({std::move(timestamp), severity, std::move(component), std::move(message)});
    while (logBuffer_.size() > kMaxLogLines) logBuffer_.pop_front();
}
}
