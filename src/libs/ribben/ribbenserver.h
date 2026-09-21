//-----------------------------------------------------------------------------
//  @file   ribbenserver.h
//
//  @brief
//  Ribben addon: the embedded live JSON-RPC server. One newline-delimited
//  JSON object per request/response, e.g.
//
//      {"id":1,"method":"ping","token":"<token>"}\n
//      {"jsonrpc":"2.0","id":1,"result":{"ok":true}}\n
//
//  Runs entirely on the thread it was created on (the GUI thread, in
//  practice) via Qt's own event loop -- there is no worker thread and no
//  blocking accept() loop, so every RibbenHost method below runs exactly
//  like a menu action's slot would. Bound to loopback only in this version;
//  the allow-list (RibbenIpFilter) is defense in depth on top of that, and
//  every request must additionally present the token from RibbenSettings.
//
//  @copyright
//  This source code is part of the Seamly2D project, a pattern making
//  program to create and model patterns of clothing.
//  Copyright (C) 2013-2026 Seamly2D project
//  <https://github.com/fashionfreedom/seamly2d> All Rights Reserved.
//
//  Seamly2D is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  Seamly2D is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with Seamly2D.  If not, see <http://www.gnu.org/licenses/>.
//-----------------------------------------------------------------------------

#ifndef RIBBENSERVER_H
#define RIBBENSERVER_H

#include <QHash>
#include <QScopedPointer>
#include <QTcpServer>

#include "ribbendispatcher.h"

class QTcpSocket;
class RibbenHost;
class RibbenIpFilter;

class RibbenServer : public QTcpServer
{
    Q_OBJECT
public:
    // Does not take ownership of host; host must outlive the server.
    explicit RibbenServer(RibbenHost *host, QObject *parent = nullptr);
    ~RibbenServer() override;

    /// Reads port/allowed-ips/token from RibbenSettings and starts
    /// listening on loopback. Returns false (and sets *errorMessage) if the
    /// port is already in use.
    bool start(QString *errorMessage = nullptr);
    void stop();

signals:
    /// Emitted when a connection is dropped for failing the IP allow-list,
    /// so the UI can surface it (e.g. in a status bar message) without this
    /// class depending on any particular widget.
    void connectionRejected(const QString &peerAddress);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    RibbenHost                    *m_host;
    RibbenDispatcher                m_dispatcher;
    QScopedPointer<RibbenIpFilter>  m_ipFilter;
    QString                          m_token;
    QHash<QTcpSocket *, QByteArray>  m_buffers;

    void        handleLine(QTcpSocket *socket, const QByteArray &line);
    void        sendResponse(QTcpSocket *socket, const QJsonObject &response);
    static void sendParseError(QTcpSocket *socket);
};

#endif // RIBBENSERVER_H
