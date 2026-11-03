#include "ServerControl.h"
#include <QLocalSocket>
#include <QTimer>

namespace {
QString controlName(const QString& pipe) { return pipe + ".control"; }
}
ServerControl::ServerControl(QObject* parent) : QObject(parent), m_server(this) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(4);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto* socket = m_server.nextPendingConnection()) {
            socket->setReadBufferSize(17);
            QTimer::singleShot(2000, socket, [socket] { socket->abort(); socket->deleteLater(); });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, buffer = QByteArray()]() mutable {
                buffer += socket->readAll();
                if (buffer.size() > 16 || (buffer.contains('\n') && buffer != "stop\n")) { socket->abort(); return; }
                if (buffer != "stop\n") return;
                socket->write("stopping\n"); socket->flush();
                // Let the acknowledgement reach the requesting process first.
                QTimer::singleShot(100, this, [this] { if (m_stop) m_stop(); });
            });
        }
    });
}
bool ServerControl::start(const QString& pipe, std::function<void()> stop) {
    m_stop = std::move(stop);
    return m_server.listen(controlName(pipe));
}
bool ServerControl::requestStop(const QString& pipe, QString& error) {
    QLocalSocket socket;
    socket.connectToServer(controlName(pipe));
    if (!socket.waitForConnected(1500)) {
        error = "Cannot connect to local server control: " + socket.errorString(); return false;
    }
    socket.write("stop\n"); socket.flush();
    QByteArray reply;
    while (!reply.contains('\n')) {
        if (!socket.bytesAvailable() && !socket.waitForReadyRead(1500)) {
            error = "Server did not acknowledge shutdown: " + socket.errorString(); return false;
        }
        reply += socket.readAll();
        if (reply.size() > 16) break;
    }
    if (reply != "stopping\n") { error = "Unexpected shutdown acknowledgement"; return false; }
    if (socket.state() != QLocalSocket::UnconnectedState && !socket.waitForDisconnected(3000)) {
        error = "Server acknowledged shutdown but did not stop"; return false;
    }
    return true;
}
