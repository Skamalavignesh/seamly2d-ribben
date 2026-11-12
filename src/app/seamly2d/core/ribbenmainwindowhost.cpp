/************************************************************************
 **
 **  @file   ribbenmainwindowhost.cpp
 **
 **  @brief
 **  Ribben addon: wires MainWindow up as a RibbenHost (see
 **  ../../../libs/ribben/ribbenhost.h) and owns the embedded RibbenServer's
 **  lifecycle -- creating it on first enable, starting/stopping it from the
 **  "Enable Ribben Addon" menu action, and implementing every RibbenHost
 **  method against this window's live doc/pattern.
 **
 **  Kept in its own file, separate from the rest of mainwindow.cpp, so the
 **  addon's surface area is easy to find, review, and (if ever needed)
 **  lift back out.
 **
 **  @copyright
 **  This source code is part of the Seamly2D project, a pattern making
 **  program to create and model patterns of clothing.
 **  Copyright (C) 2013-2026 Seamly2D project
 **  <https://github.com/fashionfreedom/seamly2d> All Rights Reserved.
 **
 **  Seamly2D is free software: you can redistribute it and/or modify
 **  it under the terms of the GNU General Public License as published by
 **  the Free Software Foundation, either version 3 of the License, or
 **  (at your option) any later version.
 **
 **  Seamly2D is distributed in the hope that it will be useful,
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 **  GNU General Public License for more details.
 **
 **  You should have received a copy of the GNU General Public License
 **  along with Seamly2D.  If not, see <http://www.gnu.org/licenses/>.
 **
 *************************************************************************/

#include "../mainwindow.h"
#include "ui_mainwindow.h"
#include "vtooloptionspropertybrowser.h"

#include "../ribben/ribbenserver.h"
#include "../ribben/ribbensettings.h"

#include "../ifc/exception/vexceptionbadid.h"
#include "../ifc/exception/vexception.h"
#include "../ifc/ifcdef.h"
#include "../ifc/xml/vdomdocument.h"
#include "../qmuparser/qmuparsererror.h"
#include "../vmisc/def.h"
#include "../vmisc/vabstractapplication.h"
#include "../vpatterndb/variables/custom_variable.h"
#include "../vpatterndb/vcontainer.h"

#include <QAction>
#include <QApplication>
#include <QDomElement>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>

#include "ribbenhostsupport.h"

using namespace RibbenSupport;

//---------------------------------------------------------------------------------------------------------------------
/// @brief initRibbenAddon adds the "Enable Ribben Addon" menu action and, if
/// the user previously left the addon enabled, starts the server right away.
/// The server itself is created lazily (on first enable) rather than here,
/// since most users will never turn it on.
//---------------------------------------------------------------------------------------------------------------------
void MainWindow::initRibbenAddon()
{
    m_ribbenAction = new QAction(tr("Enable Ribben Addon (Live MCP Server)"), this);
    m_ribbenAction->setCheckable(true);
    m_ribbenAction->setStatusTip(tr("Let a locally-running MCP client (e.g. Claude) read and edit this pattern "
                                     "live over a loopback-only JSON-RPC connection."));
    connect(m_ribbenAction, &QAction::toggled, this, &MainWindow::toggleRibbenAddon);

    ui->menuUtiliries->addSeparator();
    ui->menuUtiliries->addAction(m_ribbenAction);

    const RibbenSettings settings;
    if (settings.isEnabled())
    {
        // setChecked(true) below drives toggleRibbenAddon(true), which does
        // the actual start() call -- avoids duplicating that logic here.
        m_ribbenAction->setChecked(true);
    }
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief toggleRibbenAddon starts or stops the embedded live server and
/// persists the new on/off state so it's restored next launch.
/// @param checked true to start listening, false to stop.
//---------------------------------------------------------------------------------------------------------------------
void MainWindow::toggleRibbenAddon(bool checked)
{
    RibbenSettings settings;
    settings.setEnabled(checked);

    if (!checked)
    {
        if (m_ribbenServer != nullptr)
        {
            m_ribbenServer->stop();
        }
        setStatusMessage(tr("Ribben addon stopped."));
        return;
    }

    if (m_ribbenServer == nullptr)
    {
        m_ribbenServer = new RibbenServer(this, this);
        connect(m_ribbenServer, &RibbenServer::connectionRejected, this, [this](const QString &address)
        {
            setStatusMessage(tr("Ribben addon: rejected connection from %1 (not in the allow-list).").arg(address));
        });
    }

    QString errorMessage;
    if (!m_ribbenServer->start(&errorMessage))
    {
        m_ribbenAction->blockSignals(true);
        m_ribbenAction->setChecked(false);
        m_ribbenAction->blockSignals(false);
        settings.setEnabled(false);

        QMessageBox::warning(this, tr("Ribben addon"),
                              tr("Could not start the live MCP server: %1").arg(errorMessage));
        return;
    }

    setStatusMessage(tr("Ribben addon listening on 127.0.0.1:%1.").arg(settings.port()));
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::ping()
{
    QJsonObject result;
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("app"), QStringLiteral("seamly2d"));
    result.insert(QStringLiteral("protocol"), 1);
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief busyReason: while a modal dialog is open, its nested event loop would run a live request in the middle
/// of whatever the dialog interrupted, so RibbenDispatcher refuses requests until it is closed.
//---------------------------------------------------------------------------------------------------------------------
QString MainWindow::busyReason()
{
    if (const QWidget *modal = QApplication::activeModalWidget())
    {
        return tr("Seamly2D is showing a dialog (\"%1\"); close it, then retry.").arg(modal->windowTitle());
    }
    return QString();
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::getStatus()
{
    QJsonObject result;
    result.insert(QStringLiteral("file_path"), RibbenPatternFilePath());
    result.insert(QStringLiteral("file_name"), FileName());
    result.insert(QStringLiteral("modified"), isWindowModified());
    result.insert(QStringLiteral("unit"), UnitsToStr(qApp->patternUnit()));
    result.insert(QStringLiteral("piece_count"), static_cast<int>(pattern->DataPieces()->count()));
    result.insert(QStringLiteral("active_draft_block"), doc->getActiveDraftBlockName());
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::readPattern()
{
    QJsonObject result;
    result.insert(QStringLiteral("file_path"), RibbenPatternFilePath());
    result.insert(QStringLiteral("file_name"), FileName());
    result.insert(QStringLiteral("description"), doc->GetDescription());
    result.insert(QStringLiteral("notes"), doc->GetNotes());
    result.insert(QStringLiteral("unit"), UnitsToStr(qApp->patternUnit()));
    result.insert(QStringLiteral("piece_count"), static_cast<int>(pattern->DataPieces()->count()));
    result.insert(QStringLiteral("increment_count"), pattern->variablesData().count());
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
QJsonArray MainWindow::listIncrements()
{
    QJsonArray array;

    const QMap<QString, QSharedPointer<CustomVariable>> variables = pattern->variablesData();
    for (auto it = variables.constBegin(); it != variables.constEnd(); ++it)
    {
        // A plain "const CustomVariable *" (not the smart pointer's own
        // constness) is what actually picks the "qreal GetValue() const"
        // overload below over the mutable "qreal *GetValue()" one --
        // QSharedPointer::operator->() always yields a non-const pointee.
        const CustomVariable *variable = it.value().data();

        QJsonObject entry;
        entry.insert(QStringLiteral("name"), it.key());
        entry.insert(QStringLiteral("formula"), variable->GetFormula());
        entry.insert(QStringLiteral("value"), variable->GetValue());
        entry.insert(QStringLiteral("description"), variable->GetDescription());
        entry.insert(QStringLiteral("valid"), variable->IsFormulaOk());
        array.append(entry);
    }

    return array;
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief updateIncrement sets one increment's formula and rebuilds the
/// pattern's geometry from it -- the same "commit formula, then recompute"
/// sequence DialogVariables performs when it closes (see
/// DialogVariables::refreshPattern), just without needing the dialog open.
//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::updateIncrement(const QString &name, const QString &formula)
{
    try
    {
        pattern->getVariable<CustomVariable>(name); // throws VExceptionBadId if unknown
    }
    catch (const VExceptionBadId &error)
    {
        Q_UNUSED(error)
        throw RibbenError(RibbenError::NotFound, tr("No increment named \"%1\".").arg(name));
    }

    // Checked before anything changes: a formula that does not evaluate would make the increment invalid and could
    // break every tool that uses it.
    RibbenEvaluateFormula(pattern, formula, QStringLiteral("formula"));
    const QString previous = pattern->getVariable<CustomVariable>(name)->GetFormula();

    const auto reparse = [this]()
    {
        RibbenNonInteractiveScope nonInteractive;
        doc->LiteParseTree(Document::LiteParse);   // reports most failures by disabling the GUI, not by throwing
    };
    const auto restore = [this, &name, &previous, &reparse]()
    {
        doc->setVariableFormula(name, previous);
        try
        {
            reparse();
        }
        catch (...)
        {
            // Best effort, like ribbenReparseOrRollback: the caller gets the original error.
        }
    };

    doc->setVariableFormula(name, formula);
    try
    {
        reparse();
    }
    catch (const VException &error)
    {
        restore();
        throw RibbenError(RibbenError::InvalidFormula, error.ErrorMessage());
    }
    catch (const qmu::QmuParserError &error)
    {
        restore();
        throw RibbenError(RibbenError::InvalidFormula, error.GetMsg());
    }

    // Increments are evaluated before the draft blocks, so a formula naming geometry (Spl_..., Line_...) passes the
    // check above but is invalid in the pattern; and a value can make a dependent tool fail. Either way the
    // previous formula is restored.
    const bool incrementValid = pattern->getVariable<CustomVariable>(name)->IsFormulaOk();
    if (!guiEnabled || !incrementValid)
    {
        restore();
        throw RibbenError(RibbenError::InvalidFormula,
                          incrementValid
                          ? tr("With %1 = \"%2\" the pattern no longer builds; the previous formula \"%3\" was "
                               "restored. Seamly2D's log names the failing tool.").arg(name, formula, previous)
                          : tr("An increment cannot use \"%1\": increments are evaluated before the geometry, so "
                               "lengths and angles of the drawing are not available to them. The previous formula "
                               "\"%2\" was kept.").arg(formula, previous));
    }

    // See listIncrements() for why this needs to be a "const CustomVariable *"
    // rather than the (non-const-pointee) QSharedPointer itself.
    const CustomVariable *updated = pattern->getVariable<CustomVariable>(name).data();

    QJsonObject result;
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("formula"), updated->GetFormula());
    result.insert(QStringLiteral("value"), updated->GetValue());
    result.insert(QStringLiteral("valid"), updated->IsFormulaOk());
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::setPatternNotes(const QString &text)
{
    doc->SetNotes(text);

    QJsonObject result;
    result.insert(QStringLiteral("ok"), true);
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief ribbenReparseOrRollback re-derives the whole live scene from doc's
/// XML -- the same doc->Parse(Document::FullParse) call MainWindow::fullParseFile()
/// makes on every Undo/Redo, so a new point genuinely appears on screen
/// immediately, the same way an undone/redone one would. Called directly
/// rather than through fullParseFile() because that slot *swallows*
/// VExceptionObjectError/VExceptionConversionError internally (logs them and
/// disables the GUI) instead of propagating them -- which would leave a bad
/// live_add_point_* call invisible to the caller and the GUI silently
/// disabled. On failure here, the just-inserted element is removed and the
/// document re-parsed again to restore the prior good state before the
/// error is reported.
//---------------------------------------------------------------------------------------------------------------------
void MainWindow::ribbenReparseOrRollback(QDomElement &parent, QDomElement &child)
{
    // Non-interactive: a formula error throws (and is rolled back below) instead of opening a dialog that would
    // block this request; warnings are logged instead of shown.
    RibbenNonInteractiveScope nonInteractive;
    try
    {
        toolProperties->clearPropertyBrowser();
        setGuiEnabled(true);
        doc->Parse(Document::FullParse);
    }
    catch (const VException &error)
    {
        parent.removeChild(child);
        try
        {
            doc->Parse(Document::FullParse);
        }
        catch (const VException &)
        {
            // Best-effort restore. If this also fails, the live document was
            // already broken before our change; there's nothing more we can
            // safely do from here.
        }
        throw RibbenError(RibbenError::InvalidFormula, error.ErrorMessage());
    }
}

//---------------------------------------------------------------------------------------------------------------------
QJsonArray MainWindow::listPoints(const QString &draftBlockName)
{
    const QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    const QDomElement calculation = RibbenRequireCalculation(draftBlock);

    QJsonArray result;
    QDomElement point = calculation.firstChildElement(VAbstractPattern::TagPoint);
    while (!point.isNull())
    {
        QJsonObject entry;
        const QDomNamedNodeMap attrs = point.attributes();
        for (int i = 0; i < attrs.count(); ++i)
        {
            const QDomAttr attr = attrs.item(i).toAttr();
            entry.insert(attr.name(), attr.value());
        }
        result.append(entry);
        point = point.nextSiblingElement(VAbstractPattern::TagPoint);
    }
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::inspectGeometry(const QString &draftBlockName)
{
    const QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    const QDomElement calculation = RibbenRequireCalculation(draftBlock);
    const QDomElement modeling = RibbenRequireModeling(draftBlock);

    QJsonObject result;
    result.insert(QStringLiteral("draft_block"), draftBlockName);
    result.insert(QStringLiteral("points"), listPoints(draftBlockName));
    result.insert(QStringLiteral("lines"), RibbenListElements(calculation, VAbstractPattern::TagLine));
    result.insert(QStringLiteral("splines"), RibbenListElements(calculation, VAbstractPattern::TagSpline));
    result.insert(QStringLiteral("arcs"), RibbenListElements(calculation, VAbstractPattern::TagArc));
    // Move, rotation and mirror operations; the objects they create have no element of their own.
    result.insert(QStringLiteral("operations"), RibbenListOperations(pattern, calculation));
    QJsonObject modelingObjects;
    modelingObjects.insert(QStringLiteral("points"), RibbenListElements(modeling, VAbstractPattern::TagPoint));
    modelingObjects.insert(QStringLiteral("splines"), RibbenListElements(modeling, VAbstractPattern::TagSpline));
    modelingObjects.insert(QStringLiteral("arcs"), RibbenListElements(modeling, VAbstractPattern::TagArc));
    result.insert(QStringLiteral("modeling"), modelingObjects);
    result.insert(QStringLiteral("pieces"), listPieces(draftBlockName));
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief addPointSingle adds an anchor point at explicit (x, y) coordinates
/// -- the only point type with no dependencies -- and makes it appear on
/// screen immediately via ribbenReparseOrRollback.
//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addPointSingle(const QString &draftBlockName, const QString &name, double x, double y)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    RibbenValidateName(name);
    RibbenCheckNameFree(doc, name);

    const quint32 newId = VContainer::getNextId();
    QDomElement point = doc->createElement(VAbstractPattern::TagPoint);
    doc->SetAttribute(point, VDomDocument::AttrId, newId);
    doc->SetAttribute(point, AttrName, name);
    doc->SetAttribute(point, AttrType, RIBBEN_POINT_TYPE_SINGLE);
    doc->SetAttribute(point, AttrX, x);
    doc->SetAttribute(point, AttrY, y);
    doc->SetAttribute(point, AttrMx, 0.0);
    doc->SetAttribute(point, AttrMy, 0.0);
    calculation.appendChild(point);

    ribbenReparseOrRollback(calculation, point);

    QJsonObject result;
    result.insert(QStringLiteral("id"), static_cast<int>(newId));
    result.insert(QStringLiteral("name"), name);
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief addPointEndLine adds a point at a given length and angle from an
/// existing point, live -- length/angle are Seamly2D formulas (plain
/// numbers, increment references, or expressions), evaluated as part of
/// ribbenReparseOrRollback's full parse.
//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addPointEndLine(
    const QString &draftBlockName, const QString &name, const QString &basePoint,
    const QString &length, const QString &angle, const QString &lineType)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    RibbenValidateName(name);
    RibbenCheckNameFree(doc, name);
    const QDomElement base = RibbenFindPointByRef(doc, pattern, basePoint);
    RibbenEvaluateFormula(pattern, length, QStringLiteral("length"));
    RibbenEvaluateFormula(pattern, angle, QStringLiteral("angle"));

    const quint32 newId = VContainer::getNextId();
    QDomElement point = doc->createElement(VAbstractPattern::TagPoint);
    doc->SetAttribute(point, VDomDocument::AttrId, newId);
    doc->SetAttribute(point, AttrName, name);
    doc->SetAttribute(point, AttrType, RIBBEN_POINT_TYPE_END_LINE);
    doc->SetAttribute(point, AttrBasePoint, base.attribute(VDomDocument::AttrId));
    doc->SetAttribute(point, AttrLength, length);
    doc->SetAttribute(point, AttrAngle, angle);
    doc->SetAttribute(point, AttrLineType, lineType);
    doc->SetAttribute(point, AttrLineColor, QStringLiteral("black"));
    doc->SetAttribute(point, AttrMx, 0.0);
    doc->SetAttribute(point, AttrMy, 0.0);
    calculation.appendChild(point);

    ribbenReparseOrRollback(calculation, point);

    QJsonObject result;
    result.insert(QStringLiteral("id"), static_cast<int>(newId));
    result.insert(QStringLiteral("name"), name);
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief addPointAlongLine adds a point at a given length along the line
/// from firstPoint toward secondPoint, live.
//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addPointAlongLine(
    const QString &draftBlockName, const QString &name, const QString &firstPoint,
    const QString &secondPoint, const QString &length)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    RibbenValidateName(name);
    RibbenCheckNameFree(doc, name);
    const QDomElement first = RibbenFindPointByRef(doc, pattern, firstPoint);
    const QDomElement second = RibbenFindPointByRef(doc, pattern, secondPoint);
    RibbenEvaluateFormula(pattern, length, QStringLiteral("length"));

    const quint32 newId = VContainer::getNextId();
    QDomElement point = doc->createElement(VAbstractPattern::TagPoint);
    doc->SetAttribute(point, VDomDocument::AttrId, newId);
    doc->SetAttribute(point, AttrName, name);
    doc->SetAttribute(point, AttrType, RIBBEN_POINT_TYPE_ALONG_LINE);
    doc->SetAttribute(point, AttrFirstPoint, first.attribute(VDomDocument::AttrId));
    doc->SetAttribute(point, AttrSecondPoint, second.attribute(VDomDocument::AttrId));
    doc->SetAttribute(point, AttrLength, length);
    doc->SetAttribute(point, AttrLineType, QStringLiteral("none"));
    doc->SetAttribute(point, AttrLineColor, QStringLiteral("black"));
    doc->SetAttribute(point, AttrMx, 0.0);
    doc->SetAttribute(point, AttrMy, 0.0);
    calculation.appendChild(point);

    ribbenReparseOrRollback(calculation, point);

    QJsonObject result;
    result.insert(QStringLiteral("id"), static_cast<int>(newId));
    result.insert(QStringLiteral("name"), name);
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief addLine draws a plain visual line connecting two existing points, live.
//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addLine(
    const QString &draftBlockName, const QString &firstPoint, const QString &secondPoint, const QString &lineType)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    const QDomElement first = RibbenFindPointByRef(doc, pattern, firstPoint);
    const QDomElement second = RibbenFindPointByRef(doc, pattern, secondPoint);

    const quint32 newId = VContainer::getNextId();
    QDomElement line = doc->createElement(VAbstractPattern::TagLine);
    doc->SetAttribute(line, VDomDocument::AttrId, newId);
    doc->SetAttribute(line, AttrFirstPoint, first.attribute(VDomDocument::AttrId));
    doc->SetAttribute(line, AttrSecondPoint, second.attribute(VDomDocument::AttrId));
    doc->SetAttribute(line, AttrLineType, lineType);
    doc->SetAttribute(line, AttrLineColor, QStringLiteral("black"));
    calculation.appendChild(line);

    ribbenReparseOrRollback(calculation, line);

    QJsonObject result;
    result.insert(QStringLiteral("id"), static_cast<int>(newId));
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief addSpline draws a cubic-Bezier curve between two existing points,
/// live -- mirrors the file-based add_spline tool's "simpleInteractive"
/// spline shape exactly (see xml_geometry.py).
//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addSpline(
    const QString &draftBlockName, const QString &firstPoint, const QString &secondPoint,
    const QString &angle1, const QString &length1, const QString &angle2, const QString &length2)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    const QDomElement first = RibbenFindPointByRef(doc, pattern, firstPoint);
    const QDomElement second = RibbenFindPointByRef(doc, pattern, secondPoint);
    RibbenRequire(first.attribute(VDomDocument::AttrId) != second.attribute(VDomDocument::AttrId),
                  RibbenError::InvalidParams, tr("a spline needs two different end points"));
    RibbenEvaluateFormula(pattern, angle1, QStringLiteral("angle1"));
    RibbenEvaluateFormula(pattern, angle2, QStringLiteral("angle2"));
    // Seamly2D keeps only the size of a handle length (QLineF::setAngle), so a negative one would silently draw
    // the handle the other way round.
    RibbenRequire(RibbenEvaluateFormula(pattern, length1, QStringLiteral("length1")) >= 0, RibbenError::InvalidParams,
                  tr("length1 \"%1\" is negative; point the handle the other way (angle1 + 180) instead").arg(length1));
    RibbenRequire(RibbenEvaluateFormula(pattern, length2, QStringLiteral("length2")) >= 0, RibbenError::InvalidParams,
                  tr("length2 \"%1\" is negative; point the handle the other way (angle2 + 180) instead").arg(length2));
    const quint32 duplicate = RibbenNextDuplicate(doc, RIBBEN_SPLINE_TYPE_SIMPLE_INTERACTIVE,
                                                  first.attribute(VDomDocument::AttrId),
                                                  second.attribute(VDomDocument::AttrId));

    const quint32 newId = VContainer::getNextId();
    QDomElement spline = doc->createElement(VAbstractPattern::TagSpline);
    doc->SetAttribute(spline, VDomDocument::AttrId, newId);
    doc->SetAttribute(spline, AttrType, RIBBEN_SPLINE_TYPE_SIMPLE_INTERACTIVE);
    doc->SetAttribute(spline, AttrPoint1, first.attribute(VDomDocument::AttrId));
    doc->SetAttribute(spline, AttrPoint4, second.attribute(VDomDocument::AttrId));
    doc->SetAttribute(spline, AttrAngle1, angle1);
    doc->SetAttribute(spline, AttrLength1, length1);
    doc->SetAttribute(spline, AttrAngle2, angle2);
    doc->SetAttribute(spline, AttrLength2, length2);
    doc->SetAttribute(spline, AttrColor, QStringLiteral("black"));
    if (duplicate > 0)
    {
        doc->SetAttribute(spline, AttrDuplicate, duplicate);
    }
    calculation.appendChild(spline);

    ribbenReparseOrRollback(calculation, spline);

    QJsonObject result;
    result.insert(QStringLiteral("id"), static_cast<int>(newId));
    result.insert(QStringLiteral("name"), pattern->GeometricObject<VAbstractCurve>(newId)->name());
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief addArc draws a circular arc around an existing center point, live
/// -- mirrors the file-based add_arc tool's "simple" arc shape exactly.
//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addArc(
    const QString &draftBlockName, const QString &centerPoint, const QString &radius,
    const QString &angle1, const QString &angle2)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    const QDomElement center = RibbenFindPointByRef(doc, pattern, centerPoint);
    RibbenRequire(RibbenEvaluateFormula(pattern, radius, QStringLiteral("radius")) > 0, RibbenError::InvalidParams,
                  tr("radius \"%1\" must be greater than 0").arg(radius));
    RibbenEvaluateFormula(pattern, angle1, QStringLiteral("angle1"));
    RibbenEvaluateFormula(pattern, angle2, QStringLiteral("angle2"));

    const quint32 newId = VContainer::getNextId();
    QDomElement arc = doc->createElement(VAbstractPattern::TagArc);
    doc->SetAttribute(arc, VDomDocument::AttrId, newId);
    doc->SetAttribute(arc, AttrType, RIBBEN_ARC_TYPE_SIMPLE);
    doc->SetAttribute(arc, AttrCenter, center.attribute(VDomDocument::AttrId));
    doc->SetAttribute(arc, AttrRadius, radius);
    doc->SetAttribute(arc, AttrAngle1, angle1);
    doc->SetAttribute(arc, AttrAngle2, angle2);
    doc->SetAttribute(arc, AttrColor, QStringLiteral("black"));
    calculation.appendChild(arc);

    ribbenReparseOrRollback(calculation, arc);

    QJsonObject result;
    result.insert(QStringLiteral("id"), static_cast<int>(newId));
    result.insert(QStringLiteral("name"), pattern->GeometricObject<VAbstractCurve>(newId)->name());
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
QJsonArray MainWindow::listPieces(const QString &draftBlockName)
{
    const QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    const QDomElement pieces = RibbenRequirePieces(draftBlock);

    QJsonArray result;
    QDomElement piece = pieces.firstChildElement(VAbstractPattern::TagPiece);
    while (!piece.isNull())
    {
        QJsonObject entry;
        const QDomNamedNodeMap attrs = piece.attributes();
        for (int i = 0; i < attrs.count(); ++i)
        {
            const QDomAttr attr = attrs.item(i).toAttr();
            entry.insert(attr.name(), attr.value());
        }

        QJsonArray outline;
        const QDomElement nodesEl = piece.firstChildElement(VAbstractPattern::TagNodes);
        QDomElement nodeEl = nodesEl.firstChildElement(VAbstractPattern::TagNode);
        while (!nodeEl.isNull())
        {
            QJsonObject node;
            const QDomNamedNodeMap nodeAttrs = nodeEl.attributes();
            for (int i = 0; i < nodeAttrs.count(); ++i)
            {
                const QDomAttr attr = nodeAttrs.item(i).toAttr();
                node.insert(attr.name(), attr.value());
            }
            outline.append(node);
            nodeEl = nodeEl.nextSiblingElement(VAbstractPattern::TagNode);
        }
        entry.insert(QStringLiteral("outline"), outline);

        result.append(entry);
        piece = piece.nextSiblingElement(VAbstractPattern::TagPiece);
    }
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
/// @brief addPiece builds a seam-allowance outline from existing points/
/// curves, live. Mirrors the file-based add_piece tool: every referenced
/// point/spline/arc first gets promoted into <modeling> (Seamly2D can't
/// have a <piece> reference <calculation> geometry directly), then the
/// piece's <nodes> reference those modeling ids. All references are
/// resolved and validated up front, before anything is mutated, so a bad
/// reference never leaves a partial piece behind; if the final reparse
/// still fails (e.g. a curve whose formula doesn't evaluate), every element
/// this call inserted -- all the modeling wrappers and the piece itself --
/// is rolled back together, the same rollback guarantee
/// ribbenReparseOrRollback gives a single-element insert.
//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addPiece(
    const QString &draftBlockName, const QString &name, const QJsonArray &outline,
    bool seamAllowance, const QString &seamAllowanceWidth)
{
    if (outline.size() < 2)
    {
        throw RibbenError(RibbenError::InvalidParams, QObject::tr("a piece outline needs at least 2 nodes"));
    }

    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement modeling = RibbenRequireModeling(draftBlock);
    QDomElement pieces = RibbenRequirePieces(draftBlock);

    QList<RibbenPieceNode> resolvedNodes;
    resolvedNodes.reserve(outline.size());
    for (const QJsonValue &entryValue : outline)
    {
        const QJsonObject entry = entryValue.toObject();
        const bool hasPoint = entry.contains(QStringLiteral("point"));
        const bool hasSpline = entry.contains(QStringLiteral("spline"));
        const bool hasArc = entry.contains(QStringLiteral("arc"));
        const bool hasPath = entry.contains(QStringLiteral("spline_path"));
        if (int(hasPoint) + int(hasSpline) + int(hasArc) + int(hasPath) != 1)
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QObject::tr("each outline entry must have exactly one of point/spline/arc/spline_path"));
        }

        RibbenPieceNode node;
        node.reverse = entry.value(QStringLiteral("reverse")).toBool(false);

        if (hasPoint)
        {
            const QDomElement calcEl = RibbenFindPointByRef(doc, pattern, entry.value(QStringLiteral("point")).toString());
            node.nodeType = VAbstractPattern::NodePoint;
            node.modelingType = RIBBEN_MODELING_TYPE_PLAIN;
            node.modelingTag = VAbstractPattern::TagPoint;
            node.calcId = calcEl.attribute(VDomDocument::AttrId);
        }
        else if (hasSpline)
        {
            node.nodeType = VAbstractPattern::NodeSpline;
            node.modelingType = RIBBEN_MODELING_TYPE_SPLINE;
            node.modelingTag = VAbstractPattern::TagSpline;
            node.calcId = RibbenFindPieceCurveId(doc, pattern, VAbstractPattern::TagSpline,
                                                 entry.value(QStringLiteral("spline")).toString(),
                                                 {GOType::Spline, GOType::CubicBezier});
        }
        else if (hasPath)
        {
            const RibbenCurveRef path =
                RibbenFindCurveByRef(doc, pattern, entry.value(QStringLiteral("spline_path")).toString());
            RibbenRequire(path.kind == QLatin1String("splinePath"), RibbenError::InvalidParams,
                          QObject::tr("\"%1\" is not a spline path").arg(entry.value(QStringLiteral("spline_path"))
                                                                              .toString()));
            node.nodeType = VAbstractPattern::NodeSplinePath;
            node.modelingType = RIBBEN_MODELING_TYPE_PATH;
            node.modelingTag = VAbstractPattern::TagSpline;
            node.calcId = path.element.attribute(VDomDocument::AttrId);
        }
        else
        {
            node.nodeType = VAbstractPattern::NodeArc;
            node.modelingType = RIBBEN_MODELING_TYPE_PLAIN;
            node.modelingTag = VAbstractPattern::TagArc;
            node.calcId = RibbenFindPieceCurveId(doc, pattern, VAbstractPattern::TagArc,
                                                 entry.value(QStringLiteral("arc")).toString(), {GOType::Arc});
        }
        resolvedNodes.append(node);
    }

    // Every reference validated -- now mutate. Track every inserted element
    // (modeling wrappers + the piece) so a failed reparse can roll all of
    // them back together.
    QDomElement nodesEl = doc->createElement(VAbstractPattern::TagNodes);
    QList<QDomElement> insertedModelingElements;

    for (const RibbenPieceNode &node : resolvedNodes)
    {
        const quint32 modelingId = VContainer::getNextId();
        QDomElement modelingEl = doc->createElement(node.modelingTag);
        doc->SetAttribute(modelingEl, VDomDocument::AttrId, modelingId);
        doc->SetAttribute(modelingEl, AttrType, node.modelingType);
        doc->SetAttribute(modelingEl, AttrIdObject, node.calcId);
        modeling.appendChild(modelingEl);
        insertedModelingElements.append(modelingEl);

        QDomElement nodeEl = doc->createElement(VAbstractPattern::TagNode);
        doc->SetAttribute(nodeEl, AttrIdObject, modelingId);
        doc->SetAttribute(nodeEl, AttrType, node.nodeType);
        if (node.nodeType != VAbstractPattern::NodePoint)
        {
            doc->SetAttribute(nodeEl, VAbstractPattern::AttrNodeReverse, node.reverse ? 1 : 0);
        }
        nodesEl.appendChild(nodeEl);
    }

    const quint32 pieceId = VContainer::getNextId();
    QDomElement piece = doc->createElement(VAbstractPattern::TagPiece);
    doc->SetAttribute(piece, VDomDocument::AttrId, pieceId);
    doc->SetAttribute(piece, AttrName, name);
    doc->SetAttribute(piece, RIBBEN_ATTR_PIECE_VERSION, 2);
    doc->SetAttribute(piece, AttrClosed, 1);
    doc->SetAttribute(piece, RIBBEN_ATTR_SEAM_ALLOWANCE, seamAllowance);
    if (seamAllowance)
    {
        doc->SetAttribute(piece, VAbstractPattern::AttrWidth, seamAllowanceWidth);
    }
    piece.appendChild(nodesEl);
    pieces.appendChild(piece);

    RibbenNonInteractiveScope nonInteractive;   // see ribbenReparseOrRollback
    try
    {
        toolProperties->clearPropertyBrowser();
        setGuiEnabled(true);
        doc->Parse(Document::FullParse);
    }
    catch (const VException &error)
    {
        pieces.removeChild(piece);
        for (QDomElement modelingEl : insertedModelingElements)
        {
            modeling.removeChild(modelingEl);
        }
        try
        {
            doc->Parse(Document::FullParse);
        }
        catch (const VException &)
        {
            // Best-effort restore, same as ribbenReparseOrRollback.
        }
        throw RibbenError(RibbenError::InvalidFormula, error.ErrorMessage());
    }

    QJsonObject result;
    result.insert(QStringLiteral("id"), static_cast<int>(pieceId));
    return result;
}
