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
        Busy            = 5,   // a dialog is open, or another request is still running: retry later
        PatternOpen     = 6,   // create_pattern: another pattern is open, or the one to close has unsaved changes
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

    /// Raw calculation-layer points, lines, splines, and arcs, plus native
    /// piece outlines, for verifying live mutations against the open document.
    virtual QJsonObject inspectGeometry(const QString &draftBlockName) = 0;

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

    // -- Availability --

    /// Why the host cannot serve a request right now (e.g. a modal dialog is open in Seamly2D, whose nested event
    /// loop would otherwise run the request in the middle of another operation), or an empty string.
    /// RibbenDispatcher refuses every request except ping/get_status with RibbenError::Busy while it is non-empty.
    virtual QString busyReason() = 0;

    // -- Read-only evaluation (nothing is inserted or re-parsed) --

    /// Seamly2D's own computed values for one draft block: the evaluated position of every point, each curve's
    /// length, end angles and control points, the internal variables named in `variables` (["*"] for all of
    /// them), and the value of each formula in `formulas`. Lengths and coordinates are in the pattern's unit,
    /// in scene coordinates (y grows downwards); angles in degrees.
    virtual QJsonObject evaluate(const QString &draftBlockName, const QJsonArray &variables,
                                 const QJsonArray &formulas) = 0;

    /// The value of one formula against the open pattern's current variables, or why it cannot be evaluated.
    virtual QJsonObject checkFormula(const QString &formula) = 0;

    // -- Native Seamly2D tools (each formula is evaluated before anything is inserted) --

    /// The intersection of the lines p1Line1-p2Line1 and p1Line2-p2Line2 (Seamly2D's lineIntersect tool).
    /// Refused if the lines are parallel.
    virtual QJsonObject addPointLineIntersect(
        const QString &draftBlockName, const QString &name, const QString &p1Line1, const QString &p2Line1,
        const QString &p1Line2, const QString &p2Line2) = 0;

    /// Where the axis from basePoint at `angle` meets a curve (Seamly2D's curveIntersectAxis tool). Refused if
    /// they do not meet.
    virtual QJsonObject addPointCurveIntersectAxis(
        const QString &draftBlockName, const QString &name, const QString &basePoint, const QString &curve,
        const QString &angle, const QString &lineType) = 0;

    /// The intersection of two curves (Seamly2D's pointOfIntersectionCurves tool); `vertical` ("highest" or
    /// "lowest") and `horizontal` ("leftmost" or "rightmost") choose among several crossings. Refused if the
    /// curves do not cross.
    virtual QJsonObject addPointCurvesIntersect(
        const QString &draftBlockName, const QString &name, const QString &curve1, const QString &curve2,
        const QString &vertical, const QString &horizontal) = 0;

    /// The point at `length` along a spline, spline path or arc, from its start ("forward") or end ("backward")
    /// (Seamly2D's cutSpline, cutSplinePath and cutArc tools). Refused outside 0..curve length, which Seamly2D
    /// would clamp silently.
    virtual QJsonObject addPointOnCurve(
        const QString &draftBlockName, const QString &name, const QString &curve, const QString &length,
        const QString &direction) = 0;

    /// A spline path through existing points (Seamly2D's interactive spline path). Each entry of `points` has
    /// "point" and the handle formulas: "angle2"/"length2" leave the point, "angle1"/"length1" arrive at it.
    /// Seamly2D makes every point smooth, so the two angles of a point are 180 degrees apart: give one of them
    /// (the other is derived) or both 180 degrees apart. length2 is needed except on the last point, length1
    /// except on the first.
    virtual QJsonObject addSplinePath(const QString &draftBlockName, const QJsonArray &points) = 0;

    // -- Operations: parametric copies of points and curves (Seamly2D's Move, Rotation and Mirror by line) --
    //
    // `objects` names the points and curves to copy (names or ids). Each copy is named after its source with
    // `suffix` added (A -> A<suffix>), except an arc's copy: Seamly2D names every arc Arc_<centre>_<its id>
    // (VArc::CreateName). The suffix follows Seamly2D's own rule, so no copy can take a name the pattern already
    // uses. The result lists every copy with the source it was made from. A mirrored arc is a "flipped" arc,
    // whose length Seamly2D reports as negative (VArc::GetLength).

    /// Moves copies by `length` at `angle`, then rotates them by `rotation` degrees about `rotationOrigin`: its
    /// moved position if it is one of the objects, else where it stands; without an origin, about the centre of
    /// the moved objects (Seamly2D's moving tool). A negative length is refused: give angle + 180 instead.
    virtual QJsonObject addMove(
        const QString &draftBlockName, const QJsonArray &objects, const QString &suffix, const QString &angle,
        const QString &length, const QString &rotation, const QString &rotationOrigin) = 0;

    /// Rotates copies by `angle` degrees (counter-clockwise on screen) about `origin`, which must not be one of
    /// the objects (Seamly2D's rotation tool).
    virtual QJsonObject addRotation(
        const QString &draftBlockName, const QJsonArray &objects, const QString &suffix, const QString &origin,
        const QString &angle) = 0;

    /// Mirrors copies across the line through firstPoint and secondPoint, which must be two different positions
    /// and not among the objects (Seamly2D's flippingByLine tool).
    virtual QJsonObject addMirrorByLine(
        const QString &draftBlockName, const QJsonArray &objects, const QString &suffix, const QString &firstPoint,
        const QString &secondPoint) = 0;

    // -- The pattern file (core/ribbenhost_document.cpp) --
    //
    // These never discard work and never replace a file: anything that would is refused, and nothing is changed.

    /// Creates a new pattern, as File > New does without its dialog: `unit` (cm, mm or inch), one draft block
    /// named draftBlockName with Seamly2D's base point, saved at once to filePath (a full path ending in .sm2d
    /// that does not exist yet, in an existing folder). Refused with PatternOpen while a pattern is open, unless
    /// closeFile names exactly that open file and it has no unsaved changes: it is then closed first, as
    /// File > Close Pattern does.
    virtual QJsonObject createPattern(const QString &filePath, const QString &draftBlockName, const QString &unit,
                                      const QString &closeFile) = 0;

    /// Saves the open pattern to its own file. filePath must name exactly that file, so a caller can never save
    /// over a different one. Refused for a read-only pattern or one in an older file format (File > Save asks
    /// before rewriting those).
    virtual QJsonObject saveOpenPattern(const QString &filePath) = 0;
};

#endif // RIBBENHOST_H
