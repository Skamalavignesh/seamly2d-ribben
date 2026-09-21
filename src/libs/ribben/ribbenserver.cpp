#include "ribbenserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpSocket>

#include "ribbenhost.h"
#include "ribbenipfilter.h"
#include "ribbensettings.h"

RibbenServer::RibbenServer(RibbenHost *host, QObject *parent)
    : QTcpServer(parent)
    , m_host(host)
    , m_dispatcher(host)
    , m_ipFilter()
    , m_token()
    , m_buffers()
{}

RibbenServer::~RibbenServer()
{
    stop();
}

bool RibbenServer::start(QString *errorMessage)
{
    if (isListening())
    {
        return true;
    }

    RibbenSettings settings;
    m_token = settings.token();
    m_ipFilter.reset(new RibbenIpFilter(settings.allowedIps()));

    // Loopback only in this version -- see the file header for why.
    if (!listen(QHostAddress::LocalHost, settings.port()))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = errorString();
        }
        return false;
    }

    return true;
}

void RibbenServer::stop()
{
    if (isListening())
    {
        close();
    }

    for (auto it = m_buffers.constBegin(); it != m_buffers.constEnd(); ++it)
    {
        it.key()->disconnectFromHost();
        it.key()->deleteLater();
    }
    m_buffers.clear();
}

void RibbenServer::incomingConnection(qintptr socketDescriptor)
{
    QTcpSocket *socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor))
    {
        socket->deleteLater();
        return;
    }

    if (m_ipFilter && !m_ipFilter->isAllowed(socket->peerAddress()))
    {
        emit connectionRejected(socket->peerAddress().toString());
        socket->disconnectFromHost();
        socket->deleteLater();
        return;
    }

    connect(socket, &QTcpSocket::readyRead,    this, &RibbenServer::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, &RibbenServer::onDisconnected);
    m_buffers.insert(socket, QByteArray());
}

void RibbenServer::onReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket == nullptr || !m_buffers.contains(socket))
    {
        return;
    }

    QByteArray &buffer = m_buffers[socket];
    buffer.append(socket->readAll());

    int newlineIndex;
    while ((newlineIndex = buffer.indexOf('\n')) != -1)
    {
        const QByteArray line = buffer.left(newlineIndex).trimmed();
        buffer.remove(0, newlineIndex + 1);
        if (!line.isEmpty())
        {
            handleLine(socket, line);
        }
    }
}

void RibbenServer::onDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket != nullptr)
    {
        m_buffers.remove(socket);
        socket->deleteLater();
    }
}

void RibbenServer::handleLine(QTcpSocket *socket, const QByteArray &line)
{
    QJsonParseError parseError{};
    const QJsonDocument requestDoc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !requestDoc.isObject())
    {
        sendParseError(socket);
        return;
    }

    const QJsonObject request = requestDoc.object();
    const QJsonValue id = request.value(QStringLiteral("id"));

    if (request.value(QStringLiteral("token")).toString() != m_token)
    {
        QJsonObject error;
        error.insert(QStringLiteral("code"), RibbenError::Unauthorized);
        error.insert(QStringLiteral("message"), QStringLiteral("Missing or incorrect token."));

        QJsonObject response;
        response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        response.insert(QStringLiteral("id"), id);
        response.insert(QStringLiteral("error"), error);
        sendResponse(socket, response);
        return;
    }

    sendResponse(socket, m_dispatcher.dispatch(request));
}

void RibbenServer::sendResponse(QTcpSocket *socket, const QJsonObject &response)
{
    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
}

void RibbenServer::sendParseError(QTcpSocket *socket)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), -32700);
    error.insert(QStringLiteral("message"), QStringLiteral("Invalid JSON."));

    QJsonObject response;
    response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
    response.insert(QStringLiteral("id"), QJsonValue());
    response.insert(QStringLiteral("error"), error);

    socket->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
    socket->write("\n");
}
