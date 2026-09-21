//-----------------------------------------------------------------------------
//  @file   ribbenipfilter.h
//
//  @brief
//  Ribben addon: parses a comma-separated list of allowed IPs/CIDR subnets
//  and checks incoming connections against it, mirroring the allow-list
//  freecad-mcp's addon applies to its own RPC server (see
//  freecad-mcp/addon/FreeCADMCP/rpc_server/ip_filter.py) so both addons
//  behave the same way for anyone already used to one of them.
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

#ifndef RIBBENIPFILTER_H
#define RIBBENIPFILTER_H

#include <QHostAddress>
#include <QList>
#include <QPair>
#include <QString>

class RibbenIpFilter
{
public:
    // Each entry is a single IP or CIDR subnet (e.g. "127.0.0.1", "::1",
    // "192.168.1.0/24"). Takes an already-split list -- RibbenSettings
    // stores the allow-list as a QStringList precisely so nothing here or
    // upstream needs to split/join a comma-separated string (see
    // ribbensettings.h for why that was fragile).
    explicit RibbenIpFilter(const QStringList &allowedIps);

    bool isAllowed(const QHostAddress &address) const;

    /// Validates a comma-separated allow-list a settings UI might collect
    /// from a single free-text field. Returns the individually-invalid
    /// entries (empty means the whole string parsed cleanly); the caller
    /// still needs to split the result into a QStringList before handing it
    /// to RibbenSettings::setAllowedIps() or this class's constructor.
    static QStringList invalidEntries(const QString &allowedIpsCsv);

private:
    // address/prefixLength pairs, as returned by QHostAddress::parseSubnet.
    QList<QPair<QHostAddress, int>> m_subnets;
};

#endif // RIBBENIPFILTER_H
