//-----------------------------------------------------------------------------
//  @file   ribbenhost_operations.cpp
//
//  @brief
//  Ribben addon: Seamly2D's own operation tools for live clients -- Move
//  (translate, then optionally rotate), Rotation and Mirror by line. Each makes
//  new, parametric copies of the chosen points and curves, named after them
//  with a suffix (A -> A<suffix>; an arc's copy gets Seamly2D's own arc name,
//  Arc_<centre>_<its id>), and stays linked to its sources: when a
//  source or a formula changes, the copies follow.
//
//  As in ribbenhost_nativetools.cpp, every request is checked completely before
//  anything is inserted -- references, formulas, the suffix, and the cases
//  Seamly2D's own dialogs refuse or quietly correct -- and insertion and
//  rollback work as in ribbenmainwindowhost.cpp (ribbenReparseOrRollback).
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

#include "../qmuparser/qmudef.h"
#include "../vtools/tools/drawTools/drawtools.h"

#include <QLineF>
#include <QList>

using namespace RibbenSupport;

namespace
{
    struct OperationSource
    {
        quint32 id;
        QString name;
        GOType  type;
    };

    QString Id(const QDomElement &element)
    {
        return element.attribute(VDomDocument::AttrId);
    }

    /// The points and curves an operation works on, by name or id, each once. Seamly2D's operations handle these
    /// object types (VToolMove::Create, VToolRotation::Create, VAbstractMirror::CreateDestination).
    QList<OperationSource> ResolveObjects(VPattern *doc, const VContainer *data, const QJsonArray &objects)
    {
        RibbenRequire(!objects.isEmpty(), RibbenError::InvalidParams,
                      QObject::tr("\"objects\" must name at least one point or curve"));
        const QList<GOType> supported{GOType::Point, GOType::Arc, GOType::EllipticalArc, GOType::Spline,
                                      GOType::SplinePath, GOType::CubicBezier, GOType::CubicBezierPath};
        QList<OperationSource> resolved;
        for (const QJsonValue &value : objects)
        {
            const QString ref = value.toVariant().toString().trimmed();
            RibbenRequire(!ref.isEmpty(), RibbenError::InvalidParams,
                          QObject::tr("every entry of \"objects\" must be a point or curve name or id"));
            quint32 id = NULL_ID;
            try
            {
                id = Id(RibbenFindPointByRef(doc, data, ref)).toUInt();
            }
            catch (const RibbenError &error)
            {
                if (error.code() != RibbenError::NotFound)
                {
                    throw;
                }
                try
                {
                    id = RibbenFindCurveByRef(doc, data, ref).id;
                }
                catch (const RibbenError &curveError)
                {
                    if (curveError.code() != RibbenError::NotFound)
                    {
                        throw;
                    }
                    throw RibbenError(RibbenError::NotFound,
                                      QObject::tr("No point or curve named or with id \"%1\".").arg(ref));
                }
            }

            QSharedPointer<VGObject> object;
            try
            {
                object = data->GetGObject(id);
            }
            catch (const VException &)
            {
                throw RibbenError(RibbenError::NotFound,
                                  QObject::tr("\"%1\" exists in the document but was not built").arg(ref));
            }
            const auto type = static_cast<GOType>(object->getType());
            RibbenRequire(supported.contains(type), RibbenError::InvalidParams,
                          QObject::tr("\"%1\" is not a point or curve an operation can copy").arg(ref));
            for (const OperationSource &earlier : resolved)
            {
                RibbenRequire(earlier.id != id, RibbenError::InvalidParams,
                              QObject::tr("\"%1\" is listed twice in \"objects\"").arg(ref));
            }
            resolved.append(OperationSource{id, object->name(), type});
        }
        return resolved;
    }

    bool Contains(const QList<OperationSource> &sources, const QString &id)
    {
        for (const OperationSource &source : sources)
        {
            if (QString::number(source.id) == id)
            {
                return true;
            }
        }
        return false;
    }

    /// Seamly2D's own rule for an operation suffix (DialogMove/DialogRotation/DialogMirrorByLine::suffixChanged):
    /// not empty, and every name the pattern already uses, with the suffix added, must still be a valid name that
    /// is not used yet -- so no copy can ever take an existing name.
    void CheckSuffix(const QString &suffix)
    {
        RibbenRequire(!suffix.isEmpty(), RibbenError::InvalidParams, QObject::tr("\"suffix\" must not be empty"));
        const QRegularExpression valid(NameRegExp());
        for (const QString &name : VContainer::AllUniqueNames())
        {
            const QString renamed = name + suffix;
            RibbenRequire(valid.match(renamed).hasMatch(), RibbenError::InvalidParams,
                          QObject::tr("the suffix \"%1\" makes the invalid name \"%2\"; use letters, digits or _")
                              .arg(suffix, renamed));
            RibbenRequire(VContainer::IsUnique(renamed), RibbenError::InvalidParams,
                          QObject::tr("with the suffix \"%1\" a copy could be named \"%2\", which the pattern already "
                                      "uses; choose another suffix").arg(suffix, renamed));
        }
    }

    QDomElement NewOperation(VPattern *doc, quint32 id, const QString &type, const QString &suffix)
    {
        QDomElement operation = doc->createElement(VAbstractPattern::TagOperation);
        doc->SetAttribute(operation, VDomDocument::AttrId, id);
        doc->SetAttribute(operation, AttrType, type);
        doc->SetAttribute(operation, AttrSuffix, suffix);
        return operation;
    }

    /// The <source> and <destination> lists, as VAbstractOperation::SaveSourceDestination writes them. A new id is
    /// reserved for every copy; a point's copy keeps its source's label offset, as Seamly2D's own tools do.
    QList<quint32> AppendSourceDestination(VPattern *doc, const VContainer *data, QDomElement &operation,
                                           const QList<OperationSource> &sources)
    {
        QList<quint32> created;
        QDomElement sourceList = doc->createElement(RIBBEN_OPERATION_TAG_SOURCE);
        QDomElement destinationList = doc->createElement(RIBBEN_OPERATION_TAG_DESTINATION);
        for (const OperationSource &source : sources)
        {
            QDomElement item = doc->createElement(RIBBEN_OPERATION_TAG_ITEM);
            doc->SetAttribute(item, AttrIdObject, source.id);
            doc->SetAttribute(item, AttrLineType, LineTypeSolidLine);
            doc->SetAttribute(item, AttrColor, ColorBlack);
            sourceList.appendChild(item);

            const quint32 newId = VContainer::getNextId();
            QDomElement copy = doc->createElement(RIBBEN_OPERATION_TAG_ITEM);
            doc->SetAttribute(copy, AttrIdObject, newId);
            if (source.type == GOType::Point)
            {
                const QSharedPointer<VPointF> point = data->GeometricObject<VPointF>(source.id);
                doc->SetAttribute(copy, AttrMx, qApp->fromPixel(point->mx()));
                doc->SetAttribute(copy, AttrMy, qApp->fromPixel(point->my()));
                doc->SetAttribute<bool>(copy, AttrShowPointName, point->isShowPointName());
            }
            destinationList.appendChild(copy);
            created.append(newId);
        }
        operation.appendChild(sourceList);
        operation.appendChild(destinationList);
        return created;
    }

    /// The operation's id and type, and every copy with the source it was made from.
    QJsonObject OperationResult(const VContainer *data, quint32 id, const QString &type, const QString &suffix,
                                const QList<OperationSource> &sources, const QList<quint32> &created)
    {
        QJsonArray objects;
        for (int i = 0; i < sources.size(); ++i)
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("source"), sources.at(i).name);
            entry.insert(QStringLiteral("source_id"), static_cast<int>(sources.at(i).id));
            entry.insert(QStringLiteral("id"), static_cast<int>(created.at(i)));
            entry.insert(QStringLiteral("name"), data->GetGObject(created.at(i))->name());
            entry.insert(QStringLiteral("kind"), RibbenObjectKind(sources.at(i).type));
            objects.append(entry);
        }
        QJsonObject result;
        result.insert(QStringLiteral("id"), static_cast<int>(id));
        result.insert(QStringLiteral("type"), type);
        result.insert(QStringLiteral("suffix"), suffix);
        result.insert(QStringLiteral("objects"), objects);
        return result;
    }
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addMove(
    const QString &draftBlockName, const QJsonArray &objects, const QString &suffix, const QString &angle,
    const QString &length, const QString &rotation, const QString &rotationOrigin)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    const QList<OperationSource> sources = ResolveObjects(doc, pattern, objects);
    CheckSuffix(suffix);
    RibbenEvaluateFormula(pattern, angle, QStringLiteral("angle"));
    // Seamly2D moves by a negative length in the opposite direction; one way of writing a direction is kept.
    RibbenRequire(RibbenEvaluateFormula(pattern, length, QStringLiteral("length")) >= 0, RibbenError::InvalidParams,
                  tr("length \"%1\" is negative; give the opposite direction as angle + 180 instead").arg(length));
    RibbenEvaluateFormula(pattern, rotation, QStringLiteral("rotation"));
    QString originId;
    if (!rotationOrigin.trimmed().isEmpty())
    {
        originId = Id(RibbenFindPointByRef(doc, pattern, rotationOrigin.trimmed()));
    }

    const quint32 newId = VContainer::getNextId();
    QDomElement operation = NewOperation(doc, newId, VToolMove::ToolType, suffix);
    doc->SetAttribute(operation, AttrAngle, angle);
    doc->SetAttribute(operation, AttrLength, length);
    doc->SetAttribute(operation, AttrRotationAngle, rotation);
    if (!originId.isEmpty())
    {
        doc->SetAttribute(operation, AttrCenter, originId);
    }
    const QList<quint32> created = AppendSourceDestination(doc, pattern, operation, sources);
    calculation.appendChild(operation);

    ribbenReparseOrRollback(calculation, operation);
    return OperationResult(pattern, newId, VToolMove::ToolType, suffix, sources, created);
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addRotation(
    const QString &draftBlockName, const QJsonArray &objects, const QString &suffix, const QString &origin,
    const QString &angle)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    const QList<OperationSource> sources = ResolveObjects(doc, pattern, objects);
    CheckSuffix(suffix);
    const QString originId = Id(RibbenFindPointByRef(doc, pattern, origin));
    // Seamly2D's dialog quietly drops the origin from the objects (DialogRotation::ChosenObject); refused here.
    RibbenRequire(!Contains(sources, originId), RibbenError::InvalidParams,
                  tr("the origin %1 is one of the objects; a point does not move when rotated about itself, so "
                     "leave it out of \"objects\"").arg(origin));
    RibbenEvaluateFormula(pattern, angle, QStringLiteral("angle"));

    const quint32 newId = VContainer::getNextId();
    QDomElement operation = NewOperation(doc, newId, VToolRotation::ToolType, suffix);
    doc->SetAttribute(operation, AttrCenter, originId);
    doc->SetAttribute(operation, AttrAngle, angle);
    const QList<quint32> created = AppendSourceDestination(doc, pattern, operation, sources);
    calculation.appendChild(operation);

    ribbenReparseOrRollback(calculation, operation);
    return OperationResult(pattern, newId, VToolRotation::ToolType, suffix, sources, created);
}

//---------------------------------------------------------------------------------------------------------------------
QJsonObject MainWindow::addMirrorByLine(
    const QString &draftBlockName, const QJsonArray &objects, const QString &suffix, const QString &firstPoint,
    const QString &secondPoint)
{
    QDomElement draftBlock = RibbenRequireDraftBlock(doc, draftBlockName);
    QDomElement calculation = RibbenRequireCalculation(draftBlock);
    const QList<OperationSource> sources = ResolveObjects(doc, pattern, objects);
    CheckSuffix(suffix);
    const QDomElement first = RibbenFindPointByRef(doc, pattern, firstPoint);
    const QDomElement second = RibbenFindPointByRef(doc, pattern, secondPoint);
    RibbenRequire(Id(first) != Id(second), RibbenError::InvalidParams,
                  tr("the mirror line needs two different points"));
    // Seamly2D's dialog refuses a line point that is also one of the objects (DialogMirrorByLine::pointChanged).
    RibbenRequire(!Contains(sources, Id(first)) && !Contains(sources, Id(second)), RibbenError::InvalidParams,
                  tr("a point of the mirror line is one of the objects; leave %1 and %2 out of \"objects\"")
                      .arg(firstPoint, secondPoint));
    RibbenRequire(QLineF(RibbenPointPosition(pattern, first), RibbenPointPosition(pattern, second)).length() > 0,
                  RibbenError::InvalidParams,
                  tr("%1 and %2 are at the same position, so they give no mirror line").arg(firstPoint, secondPoint));

    const quint32 newId = VContainer::getNextId();
    QDomElement operation = NewOperation(doc, newId, VToolMirrorByLine::ToolType, suffix);
    doc->SetAttribute(operation, AttrP1Line, Id(first));
    doc->SetAttribute(operation, AttrP2Line, Id(second));
    const QList<quint32> created = AppendSourceDestination(doc, pattern, operation, sources);
    calculation.appendChild(operation);

    ribbenReparseOrRollback(calculation, operation);
    return OperationResult(pattern, newId, VToolMirrorByLine::ToolType, suffix, sources, created);
}
