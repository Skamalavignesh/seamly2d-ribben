//-----------------------------------------------------------------------------
//  @file   ribbenhostsupport.h
//
//  @brief
//  Ribben addon: helpers shared by the live JSON-RPC host methods of MainWindow
//  (ribbenmainwindowhost.cpp, ribbenhost_evaluate.cpp, ribbenhost_nativetools.cpp).
//  The first part was moved unchanged out of ribbenmainwindowhost.cpp's
//  anonymous namespace so the newer host files can use it too.
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

#ifndef RIBBENHOSTSUPPORT_H
#define RIBBENHOSTSUPPORT_H

#include "../mainwindow.h"

#include "../ribben/ribbenhost.h"

#include "../ifc/exception/vexceptionbadid.h"
#include "../ifc/exception/vexception.h"
#include "../ifc/ifcdef.h"
#include "../ifc/xml/vabstractpattern.h"
#include "../ifc/xml/vdomdocument.h"
#include "../qmuparser/qmuparsererror.h"
#include "../vgeometry/vabstractcurve.h"
#include "../vgeometry/vgeometrydef.h"
#include "../vgeometry/vgobject.h"
#include "../vgeometry/vpointf.h"
#include "../vmisc/def.h"
#include "../vmisc/vabstractapplication.h"
#include "../vpatterndb/calculator.h"
#include "../vpatterndb/vcontainer.h"
#include "../xml/vpattern.h"

#include <QDomElement>
#include <QDomNodeList>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QtNumeric>

namespace RibbenSupport
{
    // Matches VToolBasePoint::ToolType/VToolEndLine::ToolType/VToolAlongLine::ToolType
    // (src/libs/vtools/tools/drawTools/toolpoint/...) -- not #included here since
    // those are heavy QGraphicsItem-derived classes pulling in most of vtools/
    // just to reference three string literals we already know from source.
    inline const QString RIBBEN_POINT_TYPE_SINGLE    = QStringLiteral("single");
    inline const QString RIBBEN_POINT_TYPE_END_LINE  = QStringLiteral("endLine");
    inline const QString RIBBEN_POINT_TYPE_ALONG_LINE = QStringLiteral("alongLine");

    // Matches VToolSpline::ToolType / VToolArc::ToolType
    // (src/libs/vtools/tools/drawTools/toolcurve/...).
    inline const QString RIBBEN_SPLINE_TYPE_SIMPLE_INTERACTIVE = QStringLiteral("simpleInteractive");
    inline const QString RIBBEN_ARC_TYPE_SIMPLE                = QStringLiteral("simple");

    // Matches VNodePoint::ToolType/VNodeArc::ToolType ("modeling") and
    // VNodeSpline::ToolType ("modelingSpline")
    // (src/libs/vtools/tools/nodeDetails/vnode{point,arc,spline}.cpp) -- the
    // <modeling> section's own per-entry type attribute, distinct from the
    // <piece><nodes><node type="..."> values below.
    inline const QString RIBBEN_MODELING_TYPE_PLAIN  = QStringLiteral("modeling");
    inline const QString RIBBEN_MODELING_TYPE_SPLINE = QStringLiteral("modelingSpline");

    // Matches PatternPieceTool::AttrSeamAllowance/AttrVersion
    // (src/libs/vtools/tools/pattern_piece_tool.cpp) -- defined locally here
    // rather than pulling in that (heavy) tool class just for two attribute
    // name strings, same reasoning as the point-type constants above.
    inline const QString RIBBEN_ATTR_SEAM_ALLOWANCE = QStringLiteral("seamAllowance");
    inline const QString RIBBEN_ATTR_PIECE_VERSION  = QStringLiteral("version");

    inline QDomElement RibbenRequireDraftBlock(VPattern *doc, const QString &name)
    {
        QDomElement draftBlock = doc->getDraftBlockElement(name);
        if (draftBlock.isNull())
        {
            throw RibbenError(RibbenError::NotFound, QObject::tr("No draft block named \"%1\".").arg(name));
        }
        return draftBlock;
    }

    // The full path of the open pattern file, or an empty string while the pattern has never been saved.
    // (MainWindowsNoGUI::FileName() is only the base name, for display: a client cannot locate the file
    // from it.)
    inline QString RibbenPatternFilePath()
    {
        const QString path = qApp->getFilePath();
        return path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    }

    inline QDomElement RibbenRequireCalculation(const QDomElement &draftBlock)
    {
        QDomElement calculation = draftBlock.firstChildElement(VAbstractPattern::TagCalculation);
        if (calculation.isNull())
        {
            throw RibbenError(RibbenError::InternalError, QObject::tr("draft block has no <calculation> element"));
        }
        return calculation;
    }

    inline QJsonArray RibbenListElements(const QDomElement &parent, const QString &tagName)
    {
        QJsonArray result;
        QDomElement element = parent.firstChildElement(tagName);
        while (!element.isNull())
        {
            QJsonObject entry;
            const QDomNamedNodeMap attrs = element.attributes();
            for (int i = 0; i < attrs.count(); ++i)
            {
                const QDomAttr attr = attrs.item(i).toAttr();
                entry.insert(attr.name(), attr.value());
            }
            result.append(entry);
            element = element.nextSiblingElement(tagName);
        }
        return result;
    }

    //---- Objects made by operations (move, rotation, mirror) -----------------------------------------------------

    // Match VAbstractOperation::TagSource/TagDestination/TagItem
    // (src/libs/vtools/tools/drawTools/operation/vabstractoperation.cpp).
    inline const QString RIBBEN_OPERATION_TAG_SOURCE      = QStringLiteral("source");
    inline const QString RIBBEN_OPERATION_TAG_DESTINATION = QStringLiteral("destination");
    inline const QString RIBBEN_OPERATION_TAG_ITEM        = QStringLiteral("item");

    /// One object an operation created. It has no element of its own in the pattern XML -- only an <item> in its
    /// operation's <destination>, paired by position with the <source> item it was made from -- so its name and
    /// type come from the evaluated pattern.
    struct RibbenOperationObject
    {
        quint32     id;
        QString     name;
        GOType      type;
        QDomElement operation;
        quint32     sourceId;
    };

    /// "point", "spline", "splinePath", "arc" or "other": the kinds the live tools tell apart.
    inline QString RibbenObjectKind(GOType type)
    {
        switch (type)
        {
            case GOType::Point:      return QStringLiteral("point");
            case GOType::Spline:     return QStringLiteral("spline");
            case GOType::SplinePath: return QStringLiteral("splinePath");
            case GOType::Arc:        return QStringLiteral("arc");
            default:                 return QStringLiteral("other");
        }
    }

    /// Every object the pattern's operations created, in document order; one not built (yet) is left out.
    inline QList<RibbenOperationObject> RibbenOperationObjects(VPattern *doc, const VContainer *data)
    {
        QList<RibbenOperationObject> result;
        const QDomNodeList operations = doc->elementsByTagName(VAbstractPattern::TagOperation);
        for (int i = 0; i < operations.count(); ++i)
        {
            const QDomElement operation = operations.at(i).toElement();
            const QDomNodeList sources =
                operation.firstChildElement(RIBBEN_OPERATION_TAG_SOURCE).elementsByTagName(RIBBEN_OPERATION_TAG_ITEM);
            const QDomNodeList targets = operation.firstChildElement(RIBBEN_OPERATION_TAG_DESTINATION)
                                             .elementsByTagName(RIBBEN_OPERATION_TAG_ITEM);
            for (int j = 0; j < targets.count(); ++j)
            {
                const quint32 id = targets.at(j).toElement().attribute(AttrIdObject).toUInt();
                const quint32 sourceId =
                    j < sources.count() ? sources.at(j).toElement().attribute(AttrIdObject).toUInt() : NULL_ID;
                try
                {
                    const QSharedPointer<VGObject> object = data->GetGObject(id);
                    result.append(RibbenOperationObject{id, object->name(), static_cast<GOType>(object->getType()),
                                                        operation, sourceId});
                }
                catch (const VException &)
                {
                    // not built (yet)
                }
            }
        }
        return result;
    }

    /// The operations of one draft block, for inspect_geometry: each with its attributes, its source items and its
    /// destination items, every destination item with the name and kind of the object it is.
    inline QJsonArray RibbenListOperations(const VContainer *data, const QDomElement &calculation)
    {
        QJsonArray result;
        for (QDomElement operation = calculation.firstChildElement(VAbstractPattern::TagOperation);
             !operation.isNull(); operation = operation.nextSiblingElement(VAbstractPattern::TagOperation))
        {
            QJsonObject entry;
            const QDomNamedNodeMap attrs = operation.attributes();
            for (int i = 0; i < attrs.count(); ++i)
            {
                const QDomAttr attr = attrs.item(i).toAttr();
                entry.insert(attr.name(), attr.value());
            }
            for (const QString &group : {RIBBEN_OPERATION_TAG_SOURCE, RIBBEN_OPERATION_TAG_DESTINATION})
            {
                QJsonArray items;
                const QDomNodeList nodes =
                    operation.firstChildElement(group).elementsByTagName(RIBBEN_OPERATION_TAG_ITEM);
                for (int i = 0; i < nodes.count(); ++i)
                {
                    const QDomElement node = nodes.at(i).toElement();
                    QJsonObject item;
                    const QDomNamedNodeMap itemAttrs = node.attributes();
                    for (int k = 0; k < itemAttrs.count(); ++k)
                    {
                        const QDomAttr attr = itemAttrs.item(k).toAttr();
                        item.insert(attr.name(), attr.value());
                    }
                    try
                    {
                        const QSharedPointer<VGObject> object = data->GetGObject(node.attribute(AttrIdObject).toUInt());
                        item.insert(QStringLiteral("name"), object->name());
                        item.insert(QStringLiteral("kind"), RibbenObjectKind(static_cast<GOType>(object->getType())));
                    }
                    catch (const VException &)
                    {
                        // not built (yet): the item's attributes only
                    }
                    items.append(item);
                }
                entry.insert(group, items);
            }
            result.append(entry);
        }
        return result;
    }

    /// A stand-in <point> element for an operation's point, never inserted into the document: callers of
    /// RibbenFindPointByRef only read its id and name.
    inline QDomElement RibbenOperationPointElement(VPattern *doc, const RibbenOperationObject &object)
    {
        QDomElement element = doc->createElement(VAbstractPattern::TagPoint);
        element.setAttribute(VDomDocument::AttrId, QString::number(object.id));
        element.setAttribute(AttrName, object.name);
        element.setAttribute(AttrType, object.operation.attribute(AttrType));
        return element;
    }

    /// A point by its name or id: a point element of the pattern, or a point an operation created (returned as a
    /// stand-in element, see RibbenOperationPointElement).
    inline QDomElement RibbenFindPointByRef(VPattern *doc, const VContainer *data, const QString &ref)
    {
        const QDomNodeList points = doc->elementsByTagName(VAbstractPattern::TagPoint);
        for (int i = 0; i < points.count(); ++i)
        {
            const QDomElement point = points.at(i).toElement();
            if (point.attribute(AttrName) == ref || point.attribute(VDomDocument::AttrId) == ref)
            {
                return point;
            }
        }
        for (const RibbenOperationObject &object : RibbenOperationObjects(doc, data))
        {
            if (object.type == GOType::Point && (object.name == ref || QString::number(object.id) == ref))
            {
                return RibbenOperationPointElement(doc, object);
            }
        }
        throw RibbenError(RibbenError::NotFound, QObject::tr("No point named or with id \"%1\".").arg(ref));
    }

    inline void RibbenValidateName(const QString &name)
    {
        // Roughly the schema's "shortName" rules (src/libs/ifc/schema/pattern/*.xsd):
        // no leading digit, no whitespace/formula-operator characters. Kept in
        // sync with the equivalent check in the sibling seamly2d-mcp project's
        // xml_geometry.py.
        static const QRegularExpression invalidChars(QStringLiteral(R"([\s*/&|!<>^\-+.,=?:;'"])"));
        if (name.isEmpty())
        {
            throw RibbenError(RibbenError::InvalidParams, QObject::tr("name must not be empty"));
        }
        if (name.at(0).isDigit())
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QObject::tr("invalid name \"%1\": must not start with a digit").arg(name));
        }
        if (invalidChars.match(name).hasMatch())
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QObject::tr("invalid name \"%1\": must not contain whitespace or "
                                           "formula-operator characters").arg(name));
        }
    }

    inline void RibbenCheckNameFree(VPattern *doc, const QString &name)
    {
        const QDomNodeList points = doc->elementsByTagName(VAbstractPattern::TagPoint);
        for (int i = 0; i < points.count(); ++i)
        {
            if (points.at(i).toElement().attribute(AttrName) == name)
            {
                throw RibbenError(RibbenError::InvalidParams,
                                   QObject::tr("a point named \"%1\" already exists").arg(name));
            }
        }
        // Every other name the pattern uses: points made by operations (no element of their own), measurements,
        // increments and the variables named after geometry. Seamly2D's own dialogs refuse these names the same
        // way (VContainer::IsUnique; the set is rebuilt on every full parse).
        if (!VContainer::IsUnique(name))
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QObject::tr("the name \"%1\" is already used in the pattern").arg(name));
        }
    }

    inline QDomElement RibbenRequireModeling(const QDomElement &draftBlock)
    {
        QDomElement modeling = draftBlock.firstChildElement(VAbstractPattern::TagModeling);
        if (modeling.isNull())
        {
            throw RibbenError(RibbenError::InternalError, QObject::tr("draft block has no <modeling> element"));
        }
        return modeling;
    }

    inline QDomElement RibbenRequirePieces(const QDomElement &draftBlock)
    {
        QDomElement pieces = draftBlock.firstChildElement(VAbstractPattern::TagPieces);
        if (pieces.isNull())
        {
            throw RibbenError(RibbenError::InternalError, QObject::tr("draft block has no <pieces> element"));
        }
        return pieces;
    }

    // Unlike points, lines/splines/arcs have no "name" attribute -- only
    // ever referenced by the id add_line/add_spline/add_arc returned.
    inline QDomElement RibbenFindElementByTagAndId(VPattern *doc, const QString &tag, const QString &ref)
    {
        const QDomNodeList elements = doc->elementsByTagName(tag);
        for (int i = 0; i < elements.count(); ++i)
        {
            const QDomElement element = elements.at(i).toElement();
            if (element.attribute(VDomDocument::AttrId) == ref)
            {
                return element;
            }
        }
        throw RibbenError(RibbenError::NotFound, QObject::tr("No %1 with id \"%2\".").arg(tag, ref));
    }

    /// The id of a piece outline's spline or arc: an element with that id (as before), or else a curve of one of
    /// `types` that an operation created, by its id or name.
    inline QString RibbenFindPieceCurveId(VPattern *doc, const VContainer *data, const QString &tag,
                                          const QString &ref, const QList<GOType> &types)
    {
        try
        {
            return RibbenFindElementByTagAndId(doc, tag, ref).attribute(VDomDocument::AttrId);
        }
        catch (const RibbenError &error)
        {
            for (const RibbenOperationObject &object : RibbenOperationObjects(doc, data))
            {
                if (types.contains(object.type) && (object.name == ref || QString::number(object.id) == ref))
                {
                    return QString::number(object.id);
                }
            }
            throw error;
        }
    }

    /// One resolved+validated outline entry for addPiece, below -- captures
    /// everything needed to both promote the referenced calc-layer element
    /// into <modeling> and write the piece's <node> that points at it.
    struct RibbenPieceNode
    {
        QString nodeType;       // NodePoint / NodeSpline / NodeArc
        QString modelingType;   // this <modeling> entry's own "type" attribute
        QString modelingTag;    // same element tag as the calc-layer source (point/spline/arc)
        QString calcId;         // the calc-layer element's id, i.e. <modeling>'s idObject
        bool    reverse;
    };

    //---- Shared by the evaluation and native-tool features -------------------------------------------------------

    // Matches VToolSplinePath::ToolType and VNodeSplinePath::ToolType.
    inline const QString RIBBEN_SPLINE_TYPE_PATH_INTERACTIVE = QStringLiteral("pathInteractive");
    inline const QString RIBBEN_MODELING_TYPE_PATH            = QStringLiteral("modelingPath");

    /// Puts Seamly2D in non-interactive mode for the scope's lifetime (VAbstractApplication::setNonInteractive):
    /// formula errors are thrown and warnings only logged, so a re-parse run for a live request can never wait on
    /// a modal dialog.
    class RibbenNonInteractiveScope
    {
    public:
        RibbenNonInteractiveScope()
            : m_previous(qApp->isNonInteractive())
        {
            qApp->setNonInteractive(true);
        }
        ~RibbenNonInteractiveScope() { qApp->setNonInteractive(m_previous); }
        RibbenNonInteractiveScope(const RibbenNonInteractiveScope &) = delete;
        RibbenNonInteractiveScope &operator=(const RibbenNonInteractiveScope &) = delete;

    private:
        bool m_previous;
    };

    /// The value of a Seamly2D formula against the open pattern's current variables, computed before anything is
    /// inserted. A formula Seamly2D cannot parse, or whose value is not a finite number, is refused with
    /// RibbenError::InvalidFormula naming the parameter: inserted, the first would open a modal "wrong formula"
    /// dialog and the second would silently become 0.
    inline qreal RibbenEvaluateFormula(const VContainer *data, const QString &formula, const QString &parameter)
    {
        if (formula.trimmed().isEmpty())
        {
            throw RibbenError(RibbenError::InvalidFormula, QObject::tr("%1: the formula is empty").arg(parameter));
        }
        qreal value = 0;
        try
        {
            QScopedPointer<Calculator> calculator(new Calculator());
            value = calculator->EvalFormula(data->DataVariables(), formula);
        }
        catch (qmu::QmuParserError &error)
        {
            throw RibbenError(RibbenError::InvalidFormula, QObject::tr("%1: \"%2\" cannot be evaluated: %3")
                                                               .arg(parameter, formula, error.GetMsg()));
        }
        if (!qIsFinite(value))
        {
            throw RibbenError(RibbenError::InvalidFormula,
                              QObject::tr("%1: \"%2\" does not give a finite number").arg(parameter, formula));
        }
        return value;
    }

    inline void RibbenRequire(bool condition, int code, const QString &message)
    {
        if (!condition)
        {
            throw RibbenError(code, message);
        }
    }

    /// The evaluated scene position of a calculation-layer point.
    inline QPointF RibbenPointPosition(const VContainer *data, const QDomElement &point)
    {
        return static_cast<QPointF>(*data->GeometricObject<VPointF>(point.attribute(VDomDocument::AttrId).toUInt()));
    }

    struct RibbenCurveRef
    {
        QDomElement element;
        quint32     id;
        QString     kind;   // "spline", "splinePath", "arc" or "other"
        QString     name;   // its variable name: Spl_A_B, SplPath_A_C, Arc_...
    };

    /// A calculation-layer curve, by the id add_spline/add_spline_path/add_arc returned or by its variable name.
    inline RibbenCurveRef RibbenFindCurveByRef(VPattern *doc, const VContainer *data, const QString &ref)
    {
        for (const QString &tag : {VAbstractPattern::TagSpline, VAbstractPattern::TagArc})
        {
            const QDomNodeList elements = doc->elementsByTagName(tag);
            for (int i = 0; i < elements.count(); ++i)
            {
                const QDomElement element = elements.at(i).toElement();
                if (element.parentNode().toElement().tagName() != VAbstractPattern::TagCalculation)
                {
                    continue;   // a <modeling> copy
                }
                const quint32 id = element.attribute(VDomDocument::AttrId).toUInt();
                QString name;
                try
                {
                    name = data->GeometricObject<VAbstractCurve>(id)->name();
                }
                catch (const VException &)
                {
                    // not built (yet): only the id can match
                }
                if (element.attribute(VDomDocument::AttrId) == ref || (!name.isEmpty() && name == ref))
                {
                    const QString type = element.attribute(AttrType);
                    QString kind = QStringLiteral("other");
                    if (tag == VAbstractPattern::TagSpline && type == RIBBEN_SPLINE_TYPE_SIMPLE_INTERACTIVE)
                    {
                        kind = QStringLiteral("spline");
                    }
                    else if (tag == VAbstractPattern::TagSpline && type == RIBBEN_SPLINE_TYPE_PATH_INTERACTIVE)
                    {
                        kind = QStringLiteral("splinePath");
                    }
                    else if (tag == VAbstractPattern::TagArc && type == RIBBEN_ARC_TYPE_SIMPLE)
                    {
                        kind = QStringLiteral("arc");
                    }
                    return RibbenCurveRef{element, id, kind, name};
                }
            }
        }
        // A curve an operation created: a stand-in element carrying its id (see RibbenOperationPointElement).
        for (const RibbenOperationObject &object : RibbenOperationObjects(doc, data))
        {
            if (object.type != GOType::Point && (object.name == ref || QString::number(object.id) == ref))
            {
                QDomElement element = doc->createElement(object.type == GOType::Arc ? VAbstractPattern::TagArc
                                                                                    : VAbstractPattern::TagSpline);
                element.setAttribute(VDomDocument::AttrId, QString::number(object.id));
                element.setAttribute(AttrType, object.operation.attribute(AttrType));
                return RibbenCurveRef{element, object.id, RibbenObjectKind(object.type), object.name};
            }
        }
        throw RibbenError(RibbenError::NotFound, QObject::tr("No curve with id or name \"%1\".").arg(ref));
    }

    /// Seamly2D names a curve after its end points (Spl_A_B, SplPath_A_C). A second curve of the same type between
    /// the same points needs a "duplicate" number, or its name and the formula variables named after it collide.
    inline quint32 RibbenNextDuplicate(VPattern *doc, const QString &type, const QString &firstId,
                                       const QString &lastId)
    {
        bool found = false;
        quint32 highest = 0;
        const QDomNodeList elements = doc->elementsByTagName(VAbstractPattern::TagSpline);
        for (int i = 0; i < elements.count(); ++i)
        {
            const QDomElement element = elements.at(i).toElement();
            if (element.parentNode().toElement().tagName() != VAbstractPattern::TagCalculation ||
                element.attribute(AttrType) != type)
            {
                continue;
            }
            QString first;
            QString last;
            if (type == RIBBEN_SPLINE_TYPE_SIMPLE_INTERACTIVE)
            {
                first = element.attribute(AttrPoint1);
                last = element.attribute(AttrPoint4);
            }
            else
            {
                const QDomNodeList pathPoints = element.elementsByTagName(AttrPathPoint);
                if (pathPoints.isEmpty())
                {
                    continue;
                }
                first = pathPoints.at(0).toElement().attribute(AttrPSpline);
                last = pathPoints.at(pathPoints.count() - 1).toElement().attribute(AttrPSpline);
            }
            if (first == firstId && last == lastId)
            {
                found = true;
                highest = qMax(highest, element.attribute(AttrDuplicate, QStringLiteral("0")).toUInt());
            }
        }
        return found ? highest + 1 : 0;
    }
}

#endif // RIBBENHOSTSUPPORT_H
