#pragma once
#include "app/ApplicationController.h"
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <deque>

namespace rtsp {
// A minimal, dependency-free HTTP/1.1 server (built on QTcpServer, already
// linked via Qt6::Network) exposing a small JSON API plus one embedded HTML
// page, so the relay can be watched and controlled from a phone over
// Tailscale/ZeroTier/port-forward without needing the desktop app open.
//
// Deliberately NOT tied to any profile's relay lifecycle: this server is
// owned at the application level and keeps running whether zero, one, or
// all profiles are live, so "the stream crashed" is exactly the situation
// where the panel must still be reachable.
class WebPanelServer final : public QObject {
    Q_OBJECT
public:
    explicit WebPanelServer(ApplicationController& controller, QObject* parent = nullptr);
    // Binds and starts listening. port 0 uses the persisted/default port.
    // Returns false (and logs why) if the bind fails, e.g. port in use.
    bool start(quint16 port = 0);
    void stop();
    bool isRunning() const;
    quint16 port() const;
    // Bearer token required on every /api/* request. Generated once and
    // persisted (QSettings) so it survives restarts; regenerable from the
    // desktop app if it ever needs to be invalidated (e.g. shared by mistake).
    QString token() const { return token_; }
    QString regenerateToken();
    // Best-effort LAN-reachable URL to show the user (falls back to the
    // configured advertised host, then to localhost).
    QString suggestedUrl() const;

private:
    struct LogLine { QString timestamp; int severity; QString component; QString message; };
    struct PendingRequest { QByteArray buffer; };

    void onNewConnection();
    void onSocketReadyRead(QTcpSocket* socket);
    void handleRequest(QTcpSocket* socket, const QString& method, const QString& path,
                        const QHash<QString, QString>& headers, const QByteArray& body);
    void route(QTcpSocket* socket, const QString& method, const QString& path,
               const QHash<QString, QString>& headers, const QByteArray& body);
    bool authorized(const QHash<QString, QString>& headers) const;
    void sendResponse(QTcpSocket* socket, int status, const QByteArray& body,
                       const char* contentType);
    void sendJson(QTcpSocket* socket, int status, const QJsonDocument& doc);
    void sendJsonError(QTcpSocket* socket, int status, const QString& message);
    QJsonObject profileStatusJson(const AppConfig& profile) const;
    void recordLogLine(QString timestamp, int severity, QString component, QString message);

    ApplicationController& controller_;
    QTcpServer server_;
    QHash<QTcpSocket*, PendingRequest> pending_;
    std::deque<LogLine> logBuffer_;
    QString token_;
    static constexpr int kMaxLogLines = 300;
};
}
