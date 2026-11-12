//-----------------------------------------------------------------------------
//  @file   ribbenhost_evaluate.cpp
//
//  @brief
//  Ribben addon: read-only evaluation for live clients. Seamly2D's own computed
//  values -- positions of points, lengths and control points of curves, the
//  internal variables (Line_, AngleLine_, Spl_, ...), and the value of any
//  formula -- read from the data container after the last parse. Nothing is
//  inserted, changed or re-parsed. Before this, a client could only read the
//  raw XML attributes (formulas), never the numbers Seamly2D draws with.
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

#include "../vgeometry/varc.h"
#include "../vgeometry/vspline.h"
#include "../vgeometry/vsplinepath.h"
#include "../vpatterndb/variables/vinternalvariable.h"

#include <QHash>
#include <QJsonValue>
#include <QSharedPointer>

using namespace RibbenSupport;

namespace
{
    QString VarTypeName(VarType type)
    {
        switch (type)
        {
            case VarType::Measurement:  return QStringLiteral("measurement");
            case VarType::Variable:     return QStringLiteral("increment");
            case VarType::LineLength:   return QStringLiteral("line_length");
            case VarType::CurveLength:  return QStringLiteral("curve_length");
            case VarType::CurveCLength: return QStringLiteral("curve_handle_length");
            case VarType::LineAngle:    return QStringLiteral("line_angle");
            case VarType::CurveAngle:   return QStringLiteral("curve_angle");
            case VarType::ArcRadius:    return QStringLiteral("arc_radius");
            default:                    return QStringLiteral("unknown");
        }
    }

    QJsonArray PointJson(const QPointF &point, Unit unit)
    {
        return QJsonArray{FromPixel(point.x(), unit), FromPixel(point.y(), unit)};
    }

    QJsonArray ControlPointsJson(const VSpline &spline, Unit unit)
    {
        return QJsonArray{PointJson(static_cast<QPointF>(spline.GetP1()), unit),
                          PointJson(static_cast<QPointF>(spline.GetP2()), unit),
                          PointJson(static_cast<QPointF>(spline.GetP3()), unit),
                          PointJson(static_cast<QPointF>(spline.GetP4()), unit)};
    }

    QJsonObject FormulaJson(const VContainer *data, const QString &formula)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("formula"), formula);
        try
        {
            entry.insert(QStringLiteral("value"), RibbenEvaluateFormula(data, formula, QStringLiteral("formula")));
            entry.insert(QStringLiteral("valid"), true);
        }
        catch (const RibbenError &error)
        {
            entry.insert(QStringLiteral("valid"), false);
            entry.insert(QStringLiteral("error"), error.message());
        }
        return entry;
    }

    /// A curve element's kind: "spline", "splinePath", "arc" or "other" (see RibbenFindCurveByRef).
    QString ElementCurveKind(const QDomElement &element)
    {
        const QString tag = element.tagName();
        const QString type = element.attribute(AttrType);
        if (tag == VAbstractPattern::TagSpline && type == RIBBEN_SPLINE_TYPE_SIMPLE_INTERACTIVE)
        {
            return QStringLiteral("spline");
        }
        if (tag == VAbstractPattern::TagSpline && type == RIBBEN_SPLINE_TYPE_PATH_INTERACTIVE)
        {
            return QStringLiteral("splinePath");
        }
        if (tag == VAbstractPattern::TagArc && type == RIBBEN_ARC_TYPE_SIMPLE)
        {
            return QStringLiteral("arc");
        }
        return QStringLiteral("other");
    }

    /// `tag` and `type` are the element's; for a curve an operation created, "operation" and the operation's type.
    QJsonObject CurveJson(const VContainer *data, quint32 id, const QString &tag, const QString &type,
                          const QString &kind, Unit unit)
    {
        const QSharedPointer<VAbstractCurve> curve = data->GeometricObject<VAbstractCurve>(id);

        QJsonObject entry;
        entry.insert(QStringLiteral("id"), static_cast<int>(id));
        entry.insert(QStringLiteral("tag"), tag);
        entry.insert(QStringLiteral("type"), type);
        entry.insert(QStringLiteral("name"), curve->name());
        entry.insert(QStringLiteral("length"), FromPixel(curve->GetLength(), unit));
        entry.insert(QStringLiteral("start_angle"), curve->GetStartAngle());
        entry.insert(QStringLiteral("end_angle"), curve->GetEndAngle());

        if (kind == QLatin1String("spline"))
        {
            entry.insert(QStringLiteral("control_points"), ControlPointsJson(*data->GeometricObject<VSpline>(id), unit));
        }
        else if (kind == QLatin1String("splinePath"))
        {
            const QSharedPointer<VSplinePath> path = data->GeometricObject<VSplinePath>(id);
            QJsonArray segments;
            for (qint32 index = 1; index <= path->CountSubSpl(); ++index)
            {
                const VSpline segment = path->GetSpline(index);
                QJsonObject segmentEntry;
                segmentEntry.insert(QStringLiteral("length"), FromPixel(segment.GetLength(), unit));
                segmentEntry.insert(QStringLiteral("control_points"), ControlPointsJson(segment, unit));
                segments.append(segmentEntry);
            }
            entry.insert(QStringLiteral("segments"), segments);
        }
        else if (kind == QLatin1String("arc"))
        {
            const QSharedPointer<VArc> arc = data->GeometricObject<VArc>(id);
            entry.insert(QStringLiteral("center"), PointJson(static_cast<QPointF>(arc->GetCenter()), unit));
            entry.insert(QStringLiteral("radius"), FromPixel(arc->GetRadius(), unit));
        }
        return entry;
    }

    QJsonObject VariableJson(const VInternalVariable *variable)
    {
        QJsonObject entry;
        entry.insert(QStringLiteral("value"), variable->GetValue());
        entry.insert(QStringLiteral("type"), VarTypeName(variable->GetType()));
        return entry;
    }
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::evaluate(const QString &draftBlockName, const QJsonArray &variables, const QJsonArray &formulas)
{
    const QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    const QDomElement calculation = RibbenRequireCalculation(draftBlock);
    const Unit unit = qApp->patternUnit();

    QJsonArray points;
    QJsonArray curves;
    QJsonArray notEvaluated;
    for (QDomElement element = calculation.firstChildElement(); !element.isNull();
         element = element.nextSiblingElement())
    {
        const QString tag = element.tagName();
        try
        {
            if (tag == VAbstractPattern::TagPoint)
            {
                const quint32 id = element.attribute(VDomDocument::AttrId).toUInt();
                const QSharedPointer<VPointF> point = pattern->GeometricObject<VPointF>(id);
                QJsonObject entry;
                entry.insert(QStringLiteral("id"), static_cast<int>(id));
                entry.insert(QStringLiteral("name"), point->name());
                entry.insert(QStringLiteral("type"), element.attribute(AttrType));
                entry.insert(QStringLiteral("x"), FromPixel(point->x(), unit));
                entry.insert(QStringLiteral("y"), FromPixel(point->y(), unit));
                points.append(entry);
            }
            else if (tag == VAbstractPattern::TagSpline || tag == VAbstractPattern::TagArc)
            {
                curves.append(CurveJson(pattern, element.attribute(VDomDocument::AttrId).toUInt(), tag,
                                        element.attribute(AttrType), ElementCurveKind(element), unit));
            }
        }
        catch (const VException &error)
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("id"), element.attribute(VDomDocument::AttrId));
            entry.insert(QStringLiteral("tag"), tag);
            entry.insert(QStringLiteral("reason"), error.ErrorMessage());
            notEvaluated.append(entry);
        }
    }

    // The points and curves this block's move, rotation and mirror operations created (no elements of their own).
    for (const RibbenOperationObject &object : RibbenOperationObjects(doc, pattern))
    {
        if (object.operation.parentNode() != calculation)
        {
            continue;
        }
        const QString operationType = object.operation.attribute(AttrType);
        try
        {
            if (object.type == GOType::Point)
            {
                const QSharedPointer<VPointF> point = pattern->GeometricObject<VPointF>(object.id);
                QJsonObject entry;
                entry.insert(QStringLiteral("id"), static_cast<int>(object.id));
                entry.insert(QStringLiteral("name"), point->name());
                entry.insert(QStringLiteral("type"), operationType);
                entry.insert(QStringLiteral("operation"), object.operation.attribute(VDomDocument::AttrId));
                entry.insert(QStringLiteral("x"), FromPixel(point->x(), unit));
                entry.insert(QStringLiteral("y"), FromPixel(point->y(), unit));
                points.append(entry);
            }
            else
            {
                QJsonObject entry = CurveJson(pattern, object.id, VAbstractPattern::TagOperation, operationType,
                                              RibbenObjectKind(object.type), unit);
                entry.insert(QStringLiteral("operation"), object.operation.attribute(VDomDocument::AttrId));
                curves.append(entry);
            }
        }
        catch (const VException &error)
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("id"), static_cast<int>(object.id));
            entry.insert(QStringLiteral("tag"), VAbstractPattern::TagOperation);
            entry.insert(QStringLiteral("reason"), error.ErrorMessage());
            notEvaluated.append(entry);
        }
    }

    // Only the variables asked for; "*" asks for all of them (a pattern can have thousands).
    const QHash<QString, QSharedPointer<VInternalVariable>> *all = pattern->DataVariables();
    QJsonObject values;
    QJsonArray missing;
    if (variables.contains(QJsonValue(QStringLiteral("*"))))
    {
        for (auto it = all->constBegin(); it != all->constEnd(); ++it)
        {
            values.insert(it.key(), VariableJson(it.value().data()));
        }
    }
    else
    {
        for (const QJsonValue &requested : variables)
        {
            const QString name = requested.toString();
            const auto it = all->constFind(name);
            if (it == all->constEnd())
            {
                missing.append(name);
            }
            else
            {
                values.insert(name, VariableJson(it.value().data()));
            }
        }
    }

    QJsonArray formulaResults;
    for (const QJsonValue &formula : formulas)
    {
        formulaResults.append(FormulaJson(pattern, formula.toString()));
    }

    QJsonObject result;
    result.insert(QStringLiteral("draft_block"), draftBlockName);
    result.insert(QStringLiteral("unit"), UnitsToStr(unit));
    result.insert(QStringLiteral("coordinates"), QStringLiteral("scene: x grows right, y grows down; angles in degrees, "
                                                                "counter-clockwise on screen, 0 at 3 o'clock"));
    result.insert(QStringLiteral("points"), points);
    result.insert(QStringLiteral("curves"), curves);
    result.insert(QStringLiteral("variables"), values);
    result.insert(QStringLiteral("missing_variables"), missing);
    result.insert(QStringLiteral("formulas"), formulaResults);
    result.insert(QStringLiteral("not_evaluated"), notEvaluated);
    return result;
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::checkFormula(const QString &formula)
{
    return FormulaJson(pattern, formula);
}
