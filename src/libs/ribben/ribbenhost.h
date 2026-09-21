//-----------------------------------------------------------------------------
//  @file   ribbenhost.h
//
//  @brief
//  Ribben addon: the abstract seam between the live JSON-RPC server
//  (RibbenServer/RibbenDispatcher, below) and whatever object actually holds
//  an open pattern (MainWindow, in the seamly2d app). This header has no
//  dependency on the app or on any pattern-editing class, which keeps this
//  library link-order-independent: the app implements RibbenHost and hands
//  the implementation to RibbenServer, rather than this library reaching
//  "up" into app-level classes.
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

#ifndef RIBBENHOST_H
#define RIBBENHOST_H

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

/**
 * @brief Error thrown by a RibbenHost method to report a failed request
 * (e.g. "no pattern is open", "unknown increment name"). RibbenDispatcher
 * catches this and turns it into a JSON-RPC error response; it never
 * crosses the socket boundary as a raw C++ exception.
 */
class RibbenError
{
public:
    RibbenError(int code, const QString &message)
        : m_code(code)
        , m_message(message)
    {}

    int code() const { return m_code; }
    QString message() const { return m_message; }

    // Standard JSON-RPC 2.0 reserved codes, plus our own range (>= 1).
    enum Code
    {
        InvalidRequest  = -32600,
        MethodNotFound  = -32601,
        InvalidParams   = -32602,
        InternalError   = -32603,
        Unauthorized    = 1,
        NoPatternOpen   = 2,
        NotFound        = 3,
        InvalidFormula  = 4,
    };

private:
    int     m_code;
    QString m_message;
};

/**
 * @brief RibbenHost is what a live JSON-RPC request is actually dispatched
 * against: a running Seamly2D window with a pattern open. Every method
 * mirrors a tool already exposed by the file-based "seamly2d MCP" server,
 * but operates on the live, in-memory document instead of a file on disk,
 * so edits are reflected immediately in the open window (and the file-based
 * server's read tools keep working once the user saves).
 *
 * Implementations run on the GUI thread (RibbenServer never hands work to a
 * worker thread), so it's safe for a method to touch Qt widgets/doc classes
 * directly, the same way a menu action's slot would.
 */
class RibbenHost
{
public:
    virtual ~RibbenHost() = default;

    /// Health check. Always succeeds if a host is reachable at all.
    virtual QJsonObject ping() = 0;

    /// Current file path, unsaved-changes flag, unit, and piece count.
    virtual QJsonObject getStatus() = 0;

    /// Description, notes, unit, and increment (custom variable) count.
    /// Throws RibbenError(NoPatternOpen) if nothing is open.
    virtual QJsonObject readPattern() = 0;

    /// name/formula/value/description for every increment (custom variable)
    /// in the open pattern. Throws RibbenError(NoPatternOpen) if nothing is
    /// open.
    virtual QJsonArray listIncrements() = 0;

    /// Sets one increment's formula and rebuilds the pattern's geometry from
    /// it (the same "set formula, then recompute" sequence the Variables
    /// dialog performs on close). Throws RibbenError(NotFound) if no
    /// increment has that name, or RibbenError(InvalidFormula) if the new
    /// formula doesn't evaluate.
    virtual QJsonObject updateIncrement(const QString &name, const QString &formula) = 0;

    /// Replaces the pattern's free-text notes.
    /// Throws RibbenError(NoPatternOpen) if nothing is open.
    virtual QJsonObject setPatternNotes(const QString &text) = 0;

    // -- Live geometry (draft points/lines) --
    //
    // Unlike everything above, these mutate the open pattern's actual draft
    // geometry, not just its parametric/metadata layer, and the result
    // appears on screen immediately -- the same "insert into the live XML
    // document, then run the app's own full-rebuild-from-XML pass" sequence
    // that already runs, live, every time the user hits Undo/Redo (see
    // MainWindow::fullParseFile / doc->Parse(Document::FullParse)). New
    // elements are only ever appended at the end of a draft block's
    // <calculation>, so a referenced point always already exists by the
    // time it's referenced -- dependency ordering takes care of itself.
    //
    // Covers the same handful of point types as the file-based
    // xml_geometry.py tools in the sibling seamly2d-mcp project, kept in
    // sync with it deliberately: "single" (an anchor point, no
    // dependencies), "endLine" (length+angle from an existing point),
    // "alongLine" (length along an existing line), and a plain connecting
    // line. All throw RibbenError(NotFound) for an unknown draft block or
    // referenced point, and RibbenError::InvalidParams for an invalid name.

    /// Points in one draft block, in creation order, with their raw
    /// attributes -- mirrors the file-based list_points tool.
    virtual QJsonArray listPoints(const QString &draftBlockName) = 0;

    /// Adds an anchor point at explicit (x, y) canvas coordinates (in the
    /// pattern's own unit). The only point type with no dependencies.
    virtual QJsonObject addPointSingle(const QString &draftBlockName, const QString &name, double x, double y) = 0;

    /// Adds a point at a given length and angle (each a plain number or a
    /// Seamly2D formula) from an existing point (named or referenced by id).
    virtual QJsonObject addPointEndLine(
        const QString &draftBlockName, const QString &name, const QString &basePoint,
        const QString &length, const QString &angle, const QString &lineType) = 0;

    /// Adds a point at a given length along the line from firstPoint toward
    /// secondPoint.
    virtual QJsonObject addPointAlongLine(
        const QString &draftBlockName, const QString &name, const QString &firstPoint,
        const QString &secondPoint, const QString &length) = 0;

    /// Draws a plain visual line connecting two existing points.
    virtual QJsonObject addLine(
        const QString &draftBlockName, const QString &firstPoint, const QString &secondPoint,
        const QString &lineType) = 0;

    // -- Live curves, arcs, and piece outlines --
    //
    // Same idea as the point/line methods above, kept in sync with the
    // sibling seamly2d-mcp project's file-based add_spline/add_arc/
    // add_piece tools (same attribute shapes, same defaults). All throw
    // RibbenError(NotFound) for an unknown draft block or referenced
    // point/curve.

    /// Draws a cubic-Bezier curve between two existing points (the "Curve"
    /// tool), each end with its own angle+length tangent handle.
    virtual QJsonObject addSpline(
        const QString &draftBlockName, const QString &firstPoint, const QString &secondPoint,
        const QString &angle1, const QString &length1, const QString &angle2, const QString &length2) = 0;

    /// Draws a circular arc around an existing center point (the "Arc" tool).
    virtual QJsonObject addArc(
        const QString &draftBlockName, const QString &centerPoint, const QString &radius,
        const QString &angle1, const QString &angle2) = 0;

    /// Pieces (seam-allowance outlines) in one draft block, with their raw
    /// attributes plus their outline as an array of node objects -- mirrors
    /// the file-based list_pieces tool.
    virtual QJsonArray listPieces(const QString &draftBlockName) = 0;

    /// Creates a piece (seam-allowance outline) from existing points/
    /// curves. `outline` is an ordered array of objects, each with exactly
    /// one of "point"/"spline"/"arc" (a reference: point name/id, or a
    /// spline/arc id) plus an optional "reverse" bool (ignored for point
    /// nodes). Must have at least 2 entries. Each referenced point/spline/
    /// arc is automatically promoted into the draft block's <modeling>
    /// section first -- Seamly2D's own internal representation, mirrored
    /// here -- so the caller never has to think about that layer.
    /// Throws RibbenError(InvalidParams) for fewer than 2 nodes or an
    /// ambiguous/missing point-spline-arc key in an entry.
    virtual QJsonObject addPiece(
        const QString &draftBlockName, const QString &name, const QJsonArray &outline,
        bool seamAllowance, const QString &seamAllowanceWidth) = 0;
};

#endif // RIBBENHOST_H
