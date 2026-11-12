//-----------------------------------------------------------------------------
//  @file   ribbenhost_nativetools.cpp
//
//  @brief
//  Ribben addon: Seamly2D's own construction tools for live clients that the
//  first version did not expose -- the line-line, axis-curve and curve-curve
//  intersection points, the point at a length along a curve (cut tools), and the
//  interactive spline path. Each request is checked completely before anything
//  is inserted (references, formulas, ranges, whether an intersection exists),
//  because these tools otherwise answer a bad input with a modal message box or
//  with a silently wrong point. Insertion and rollback work as in
//  ribbenmainwindowhost.cpp (ribbenReparseOrRollback).
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

#include "ribbenhostsupport.h"

#include "../vtools/tools/drawTools/drawtools.h"

#include <QLineF>
#include <QList>

using namespace RibbenSupport;

namespace
{
    QString Id(const QDomElement &element)
    {
        return element.attribute(VDomDocument::AttrId);
    }

    QDomElement NewPoint(VPattern *doc, quint32 id, const QString &name, const QString &type)
    {
        QDomElement point = doc->createElement(VAbstractPattern::TagPoint);
        doc->SetAttribute(point, VDomDocument::AttrId, id);
        doc->SetAttribute(point, AttrName, name);
        doc->SetAttribute(point, AttrType, type);
        doc->SetAttribute(point, AttrMx, 0.0);
        doc->SetAttribute(point, AttrMy, 0.0);
        return point;
    }

    QJsonObject PointResult(quint32 id, const QString &name)
    {
        QJsonObject result;
        result.insert(QStringLiteral("id"), static_cast<int>(id));
        result.insert(QStringLiteral("name"), name);
        return result;
    }

    /// A handle formula of a spline-path point: required unless the handle is never used (the arriving handle of
    /// the first point, the leaving handle of the last). Numbers are accepted as well as formula strings.
    QString HandleFormula(const QJsonObject &entry, const QString &key, bool needed, int index)
    {
        const QString value = entry.value(key).toVariant().toString().trimmed();
        if (value.isEmpty())
        {
            RibbenRequire(!needed, RibbenError::InvalidParams,
                          QObject::tr("spline path point %1 needs \"%2\"").arg(index + 1).arg(key));
            return QStringLiteral("0");
        }
        return value;
    }

    /// An optional formula: the trimmed text, or an empty string when the key is absent.
    QString OptionalFormula(const QJsonObject &entry, const QString &key)
    {
        return entry.value(key).toVariant().toString().trimmed();
    }

    struct PathPoint
    {
        QString pointId;
        QString angle1;
        QString length1;
        QString angle2;
        QString length2;
    };
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addPointLineIntersect(
    const QString &draftBlockName, const QString &name, const QString &p1Line1, const QString &p2Line1,
    const QString &p1Line2, const QString &p2Line2)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    RibbenValidateName(name);
    RibbenCheckNameFree(doc, name);
    const QDomElement a = RibbenFindPointByRef(doc, pattern, p1Line1);
    const QDomElement b = RibbenFindPointByRef(doc, pattern, p2Line1);
    const QDomElement c = RibbenFindPointByRef(doc, pattern, p1Line2);
    const QDomElement d = RibbenFindPointByRef(doc, pattern, p2Line2);
    RibbenRequire(Id(a) != Id(b) && Id(c) != Id(d), RibbenError::InvalidParams,
                  tr("each line needs two different points"));

    // Seamly2D creates no point at all for parallel lines, so they are refused here.
    QPointF crossing;
    const auto kind = QLineF(RibbenPointPosition(pattern, a), RibbenPointPosition(pattern, b))
                          .intersects(QLineF(RibbenPointPosition(pattern, c), RibbenPointPosition(pattern, d)),
                                      &crossing);
    RibbenRequire(kind != QLineF::NoIntersection, RibbenError::InvalidParams,
                  tr("the lines %1-%2 and %3-%4 are parallel").arg(p1Line1, p2Line1, p1Line2, p2Line2));

    const quint32 newId = VContainer::getNextId();
    QDomElement point = NewPoint(doc, newId, name, VToolLineIntersect::ToolType);
    doc->SetAttribute(point, AttrP1Line1, Id(a));
    doc->SetAttribute(point, AttrP2Line1, Id(b));
    doc->SetAttribute(point, AttrP1Line2, Id(c));
    doc->SetAttribute(point, AttrP2Line2, Id(d));
    calculation.appendChild(point);

    ribbenReparseOrRollback(calculation, point);
    return PointResult(newId, name);
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addPointCurveIntersectAxis(
    const QString &draftBlockName, const QString &name, const QString &basePoint, const QString &curve,
    const QString &angle, const QString &lineType)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    RibbenValidateName(name);
    RibbenCheckNameFree(doc, name);
    const QDomElement base = RibbenFindPointByRef(doc, pattern, basePoint);
    const RibbenCurveRef target = RibbenFindCurveByRef(doc, pattern, curve);
    const qreal angleValue = RibbenEvaluateFormula(pattern, angle, QStringLiteral("angle"));

    // Without an intersection Seamly2D opens a message box and puts the point at the base point; refused here.
    QPointF hit;
    const bool found = VToolCurveIntersectAxis::FindPoint(RibbenPointPosition(pattern, base), angleValue,
                                                          pattern->GeometricObject<VAbstractCurve>(target.id), &hit);
    RibbenRequire(found, RibbenError::InvalidParams,
                  tr("the axis from %1 at %2 degrees does not meet the curve %3")
                      .arg(basePoint).arg(angleValue).arg(target.name));

    const quint32 newId = VContainer::getNextId();
    QDomElement point = NewPoint(doc, newId, name, VToolCurveIntersectAxis::ToolType);
    doc->SetAttribute(point, AttrBasePoint, Id(base));
    doc->SetAttribute(point, AttrCurve, target.id);
    doc->SetAttribute(point, AttrAngle, angle);
    doc->SetAttribute(point, AttrLineType, lineType);
    doc->SetAttribute(point, AttrLineColor, QStringLiteral("black"));
    calculation.appendChild(point);

    ribbenReparseOrRollback(calculation, point);
    return PointResult(newId, name);
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addPointCurvesIntersect(
    const QString &draftBlockName, const QString &name, const QString &curve1, const QString &curve2,
    const QString &vertical, const QString &horizontal)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    RibbenValidateName(name);
    RibbenCheckNameFree(doc, name);
    RibbenRequire(vertical == QLatin1String("highest") || vertical == QLatin1String("lowest"),
                  RibbenError::InvalidParams, tr("vertical must be \"highest\" or \"lowest\", not \"%1\"").arg(vertical));
    RibbenRequire(horizontal == QLatin1String("leftmost") || horizontal == QLatin1String("rightmost"),
                  RibbenError::InvalidParams,
                  tr("horizontal must be \"leftmost\" or \"rightmost\", not \"%1\"").arg(horizontal));
    const VCrossCurvesPoint vCross = vertical == QLatin1String("highest") ? VCrossCurvesPoint::HighestPoint
                                                                          : VCrossCurvesPoint::LowestPoint;
    const HCrossCurvesPoint hCross = horizontal == QLatin1String("leftmost") ? HCrossCurvesPoint::LeftmostPoint
                                                                            : HCrossCurvesPoint::RightmostPoint;
    const RibbenCurveRef first = RibbenFindCurveByRef(doc, pattern, curve1);
    const RibbenCurveRef second = RibbenFindCurveByRef(doc, pattern, curve2);
    RibbenRequire(first.id != second.id, RibbenError::InvalidParams, tr("curve1 and curve2 are the same curve"));

    // Without a crossing Seamly2D opens a message box and puts the point at the origin; refused here.
    const QPointF hit = VToolPointOfIntersectionCurves::FindPoint(
        pattern->GeometricObject<VAbstractCurve>(first.id)->getPoints(),
        pattern->GeometricObject<VAbstractCurve>(second.id)->getPoints(), vCross, hCross);
    RibbenRequire(hit != QPointF(), RibbenError::InvalidParams,
                  tr("the curves %1 and %2 do not cross").arg(first.name, second.name));

    const quint32 newId = VContainer::getNextId();
    QDomElement point = NewPoint(doc, newId, name, VToolPointOfIntersectionCurves::ToolType);
    doc->SetAttribute(point, AttrCurve1, first.id);
    doc->SetAttribute(point, AttrCurve2, second.id);
    doc->SetAttribute(point, AttrVCrossPoint, static_cast<int>(vCross));
    doc->SetAttribute(point, AttrHCrossPoint, static_cast<int>(hCross));
    calculation.appendChild(point);

    ribbenReparseOrRollback(calculation, point);
    return PointResult(newId, name);
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addPointOnCurve(
    const QString &draftBlockName, const QString &name, const QString &curve, const QString &length,
    const QString &direction)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    RibbenValidateName(name);
    RibbenCheckNameFree(doc, name);
    RibbenRequire(direction == QLatin1String("forward") || direction == QLatin1String("backward"),
                  RibbenError::InvalidParams,
                  tr("direction must be \"forward\" or \"backward\", not \"%1\"").arg(direction));
    const RibbenCurveRef target = RibbenFindCurveByRef(doc, pattern, curve);

    QString toolType;
    QString curveAttribute;
    if (target.kind == QLatin1String("spline"))
    {
        toolType = VToolCutSpline::ToolType;
        curveAttribute = VToolCutSpline::AttrSpline;
    }
    else if (target.kind == QLatin1String("splinePath"))
    {
        toolType = VToolCutSplinePath::ToolType;
        curveAttribute = VToolCutSplinePath::AttrSplinePath;
    }
    else if (target.kind == QLatin1String("arc"))
    {
        toolType = VToolCutArc::ToolType;
        curveAttribute = AttrArc;
    }
    else
    {
        throw RibbenError(RibbenError::InvalidParams,
                          tr("%1 is not a spline, spline path or arc").arg(curve));
    }

    // Seamly2D clamps a length outside the curve silently (to its start or end); refused here instead.
    const qreal value = RibbenEvaluateFormula(pattern, length, QStringLiteral("length"));
    const qreal curveLength = FromPixel(pattern->GeometricObject<VAbstractCurve>(target.id)->GetLength(),
                                        qApp->patternUnit());
    RibbenRequire(value >= 0 && value <= curveLength, RibbenError::InvalidParams,
                  tr("length %1 is outside 0..%2, the length of %3").arg(value).arg(curveLength).arg(target.name));

    const quint32 newId = VContainer::getNextId();
    QDomElement point = NewPoint(doc, newId, name, toolType);
    doc->SetAttribute(point, curveAttribute, target.id);
    doc->SetAttribute(point, AttrLength, length);
    doc->SetAttribute(point, AttrDirection, direction);
    doc->SetAttribute(point, AttrLineColor, QStringLiteral("black"));
    calculation.appendChild(point);

    ribbenReparseOrRollback(calculation, point);
    return PointResult(newId, name);
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addSplinePath(const QString &draftBlockName, const QJsonArray &points)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    RibbenRequire(points.size() >= 2, RibbenError::InvalidParams, tr("a spline path needs at least 2 points"));

    QList<PathPoint> resolved;
    for (int index = 0; index < points.size(); ++index)
    {
        RibbenRequire(points.at(index).isObject(), RibbenError::InvalidParams,
                      tr("spline path point %1 must be an object").arg(index + 1));
        const QJsonObject entry = points.at(index).toObject();
        RibbenRequire(entry.contains(QStringLiteral("point")), RibbenError::InvalidParams,
                      tr("spline path point %1 has no \"point\"").arg(index + 1));
        const bool first = index == 0;
        const bool last = index == points.size() - 1;
        PathPoint point;
        point.pointId = Id(RibbenFindPointByRef(doc, pattern, entry.value(QStringLiteral("point")).toString()));
        point.length1 = HandleFormula(entry, QStringLiteral("length1"), !first, index);
        point.length2 = HandleFormula(entry, QStringLiteral("length2"), !last, index);

        // Seamly2D makes every spline-path point smooth: when angle1 and angle2 are not 180 degrees apart it
        // silently replaces one of them (VSplinePointData, vsplinepoint_p.h:198-216). So the missing angle is
        // derived from the given one, and two given angles must already be 180 degrees apart.
        point.angle1 = OptionalFormula(entry, QStringLiteral("angle1"));
        point.angle2 = OptionalFormula(entry, QStringLiteral("angle2"));
        RibbenRequire(!point.angle1.isEmpty() || !point.angle2.isEmpty(), RibbenError::InvalidParams,
                      tr("spline path point %1 needs \"angle1\" or \"angle2\"").arg(index + 1));
        if (point.angle1.isEmpty())
        {
            point.angle1 = QStringLiteral("(%1) + 180").arg(point.angle2);
        }
        else if (point.angle2.isEmpty())
        {
            point.angle2 = QStringLiteral("(%1) + 180").arg(point.angle1);
        }

        const QString label = tr("point %1 ").arg(index + 1);
        const qreal angle1 = RibbenEvaluateFormula(pattern, point.angle1, label + QStringLiteral("angle1"));
        const qreal angle2 = RibbenEvaluateFormula(pattern, point.angle2, label + QStringLiteral("angle2"));
        RibbenRequire(qAbs(qAbs(angle1 - angle2) - 180.0) < 1e-9, RibbenError::InvalidParams,
                      tr("%1: angle1 (%2) and angle2 (%3) must be 180 degrees apart; Seamly2D makes every spline "
                         "path point smooth and would silently change one of them. Give one angle, or both 180 "
                         "degrees apart.").arg(label.trimmed()).arg(angle1).arg(angle2));
        RibbenRequire(RibbenEvaluateFormula(pattern, point.length1, label + QStringLiteral("length1")) >= 0,
                      RibbenError::InvalidParams, tr("%1length1 is negative; turn the handle instead").arg(label));
        RibbenRequire(RibbenEvaluateFormula(pattern, point.length2, label + QStringLiteral("length2")) >= 0,
                      RibbenError::InvalidParams, tr("%1length2 is negative; turn the handle instead").arg(label));
        RibbenRequire(resolved.isEmpty() || resolved.last().pointId != point.pointId, RibbenError::InvalidParams,
                      tr("spline path points %1 and %2 are the same point").arg(index).arg(index + 1));
        resolved.append(point);
    }

    const quint32 duplicate = RibbenNextDuplicate(doc, RIBBEN_SPLINE_TYPE_PATH_INTERACTIVE, resolved.first().pointId,
                                                  resolved.last().pointId);
    const quint32 newId = VContainer::getNextId();
    QDomElement path = doc->createElement(VAbstractPattern::TagSpline);
    doc->SetAttribute(path, VDomDocument::AttrId, newId);
    doc->SetAttribute(path, AttrType, VToolSplinePath::ToolType);
    doc->SetAttribute(path, AttrColor, QStringLiteral("black"));
    if (duplicate > 0)
    {
        doc->SetAttribute(path, AttrDuplicate, duplicate);
    }
    for (const PathPoint &point : resolved)
    {
        QDomElement pathPoint = doc->createElement(AttrPathPoint);
        doc->SetAttribute(pathPoint, AttrPSpline, point.pointId);
        doc->SetAttribute(pathPoint, AttrLength1, point.length1);
        doc->SetAttribute(pathPoint, AttrLength2, point.length2);
        doc->SetAttribute(pathPoint, AttrAngle1, point.angle1);
        doc->SetAttribute(pathPoint, AttrAngle2, point.angle2);
        path.appendChild(pathPoint);
    }
    calculation.appendChild(path);

    ribbenReparseOrRollback(calculation, path);

    QJsonObject result;
    result.insert(QStringLiteral("id"), static_cast<int>(newId));
    result.insert(QStringLiteral("name"), pattern->GeometricObject<VAbstractCurve>(newId)->name());
    return result;
}
