//-----------------------------------------------------------------------------
//  @file   ribbensettings.h
//
//  @brief
//  Ribben addon: persisted on/off switch, port, allowed IPs, and auth token
//  for the embedded live JSON-RPC server. Stored under the same
//  organization/application as the rest of Seamly2D's settings (see
//  VSettings/VCommonSettings in vmisc), in its own "Ribben" group, so it
//  survives restarts without needing a separate config file.
//
//  Deliberately does not depend on VSettings/qApp: this library sits below
//  the app (src/app/seamly2d links it, not the other way around), so it
//  opens its own QSettings pointed at the same ini file instead.
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

#ifndef RIBBENSETTINGS_H
#define RIBBENSETTINGS_H

#include <QString>
#include <QStringList>

class RibbenSettings
{
public:
    RibbenSettings();

    // Off by default: the server must never start just because Seamly2D
    // was opened. The user opts in from the Utilities menu, and that
    // action is what flips this (see MainWindow::ToggleRibbenAddon).
    bool    isEnabled() const;
    void    setEnabled(bool enabled);

    quint16 port() const;
    void    setPort(quint16 port);

    // List of allowed IPs/CIDR subnets. Defaults to loopback-only, matching
    // freecad-mcp's addon default. Stored as a QStringList (Qt's ini format
    // has native list support) rather than a comma-joined QString -- a
    // hand-typed or hand-edited "a,b" string gets read back by QSettings as
    // a two-element list already, and QVariant::toString() on a multi-entry
    // list silently returns "", which previously made every connection get
    // rejected as if the allow-list were empty.
    QStringList allowedIps() const;
    void        setAllowedIps(const QStringList &allowedIps);

    // Shared secret every request must present. Generated on first use and
    // cached in settings; RibbenServer refuses any request missing it.
    QString token();

    static quint16     defaultPort();
    static QStringList defaultAllowedIps();

private:
    static QString      generateToken();
};

#endif // RIBBENSETTINGS_H
