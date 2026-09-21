//-----------------------------------------------------------------------------
//  @file   ribbendispatcher.h
//
//  @brief
//  Ribben addon: turns one parsed JSON-RPC 2.0 request object into a call
//  against a RibbenHost, and the result (or thrown RibbenError) back into a
//  JSON-RPC 2.0 response object. Kept separate from RibbenServer so the
//  request/response shape can be unit-tested without opening a socket.
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

#ifndef RIBBENDISPATCHER_H
#define RIBBENDISPATCHER_H

#include <QJsonObject>

class RibbenHost;

class RibbenDispatcher
{
public:
    // Does not take ownership; the host must outlive the dispatcher (both
    // are owned by the same MainWindow in practice, and destroyed together).
    explicit RibbenDispatcher(RibbenHost *host);

    /// Handles one already-authenticated, already-parsed JSON-RPC 2.0
    /// request object ({"id", "method", "params"}) and returns the full
    /// response object ({"id", "result"} or {"id", "error"}).
    QJsonObject dispatch(const QJsonObject &request) const;

private:
    RibbenHost *m_host;

    QJsonObject invoke(const QString &method, const QJsonObject &params) const;
};

#endif // RIBBENDISPATCHER_H
