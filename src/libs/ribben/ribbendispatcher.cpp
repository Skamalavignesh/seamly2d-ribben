#include "ribbendispatcher.h"

#include "ribbenhost.h"

namespace
{
    QJsonObject ErrorResponse(const QJsonValue &id, int code, const QString &message)
    {
        QJsonObject error;
        error.insert(QStringLiteral("code"), code);
        error.insert(QStringLiteral("message"), message);

        QJsonObject response;
        response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        response.insert(QStringLiteral("id"), id);
        response.insert(QStringLiteral("error"), error);
        return response;
    }

    QJsonObject ResultResponse(const QJsonValue &id, const QJsonValue &result)
    {
        QJsonObject response;
        response.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
        response.insert(QStringLiteral("id"), id);
        response.insert(QStringLiteral("result"), result);
        return response;
    }
}

RibbenDispatcher::RibbenDispatcher(RibbenHost *host)
    : m_host(host)
{}

QJsonObject RibbenDispatcher::dispatch(const QJsonObject &request) const
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString method = request.value(QStringLiteral("method")).toString();

    if (method.isEmpty())
    {
        return ErrorResponse(id, RibbenError::InvalidRequest, QStringLiteral("Missing \"method\"."));
    }

    const QJsonValue paramsValue = request.value(QStringLiteral("params"));
    const QJsonObject params = paramsValue.isObject() ? paramsValue.toObject() : QJsonObject();

    try
    {
        return ResultResponse(id, invoke(method, params));
    }
    catch (const RibbenError &error)
    {
        return ErrorResponse(id, error.code(), error.message());
    }
}

QJsonObject RibbenDispatcher::invoke(const QString &method, const QJsonObject &params) const
{
    if (method == QLatin1String("ping"))
    {
        return m_host->ping();
    }

    if (method == QLatin1String("get_status"))
    {
        return m_host->getStatus();
    }

    if (method == QLatin1String("read_pattern"))
    {
        return m_host->readPattern();
    }

    if (method == QLatin1String("list_increments"))
    {
        QJsonObject result;
        result.insert(QStringLiteral("increments"), m_host->listIncrements());
        return result;
    }

    if (method == QLatin1String("update_increment"))
    {
        if (!params.contains(QStringLiteral("name")) || !params.contains(QStringLiteral("formula")))
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QStringLiteral("update_increment needs \"name\" and \"formula\"."));
        }
        return m_host->updateIncrement(params.value(QStringLiteral("name")).toString(),
                                        params.value(QStringLiteral("formula")).toString());
    }

    if (method == QLatin1String("set_pattern_notes"))
    {
        if (!params.contains(QStringLiteral("text")))
        {
            throw RibbenError(RibbenError::InvalidParams, QStringLiteral("set_pattern_notes needs \"text\"."));
        }
        return m_host->setPatternNotes(params.value(QStringLiteral("text")).toString());
    }

    if (method == QLatin1String("list_points"))
    {
        if (!params.contains(QStringLiteral("draft_block_name")))
        {
            throw RibbenError(RibbenError::InvalidParams, QStringLiteral("list_points needs \"draft_block_name\"."));
        }
        QJsonObject result;
        result.insert(QStringLiteral("points"), m_host->listPoints(params.value(QStringLiteral("draft_block_name")).toString()));
        return result;
    }

    if (method == QLatin1String("add_point_single"))
    {
        if (!params.contains(QStringLiteral("draft_block_name")) || !params.contains(QStringLiteral("name")) ||
            !params.contains(QStringLiteral("x")) || !params.contains(QStringLiteral("y")))
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QStringLiteral("add_point_single needs \"draft_block_name\", \"name\", \"x\", \"y\"."));
        }
        return m_host->addPointSingle(params.value(QStringLiteral("draft_block_name")).toString(),
                                       params.value(QStringLiteral("name")).toString(),
                                       params.value(QStringLiteral("x")).toDouble(),
                                       params.value(QStringLiteral("y")).toDouble());
    }

    if (method == QLatin1String("add_point_end_line"))
    {
        if (!params.contains(QStringLiteral("draft_block_name")) || !params.contains(QStringLiteral("name")) ||
            !params.contains(QStringLiteral("base_point")) || !params.contains(QStringLiteral("length")) ||
            !params.contains(QStringLiteral("angle")))
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QStringLiteral("add_point_end_line needs \"draft_block_name\", \"name\", "
                                              "\"base_point\", \"length\", \"angle\"."));
        }
        return m_host->addPointEndLine(params.value(QStringLiteral("draft_block_name")).toString(),
                                        params.value(QStringLiteral("name")).toString(),
                                        params.value(QStringLiteral("base_point")).toString(),
                                        params.value(QStringLiteral("length")).toString(),
                                        params.value(QStringLiteral("angle")).toString(),
                                        params.value(QStringLiteral("line_type")).toString(QStringLiteral("none")));
    }

    if (method == QLatin1String("add_point_along_line"))
    {
        if (!params.contains(QStringLiteral("draft_block_name")) || !params.contains(QStringLiteral("name")) ||
            !params.contains(QStringLiteral("first_point")) || !params.contains(QStringLiteral("second_point")) ||
            !params.contains(QStringLiteral("length")))
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QStringLiteral("add_point_along_line needs \"draft_block_name\", \"name\", "
                                              "\"first_point\", \"second_point\", \"length\"."));
        }
        return m_host->addPointAlongLine(params.value(QStringLiteral("draft_block_name")).toString(),
                                          params.value(QStringLiteral("name")).toString(),
                                          params.value(QStringLiteral("first_point")).toString(),
                                          params.value(QStringLiteral("second_point")).toString(),
                                          params.value(QStringLiteral("length")).toString());
    }

    if (method == QLatin1String("add_line"))
    {
        if (!params.contains(QStringLiteral("draft_block_name")) || !params.contains(QStringLiteral("first_point")) ||
            !params.contains(QStringLiteral("second_point")))
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QStringLiteral("add_line needs \"draft_block_name\", \"first_point\", \"second_point\"."));
        }
        return m_host->addLine(params.value(QStringLiteral("draft_block_name")).toString(),
                                params.value(QStringLiteral("first_point")).toString(),
                                params.value(QStringLiteral("second_point")).toString(),
                                params.value(QStringLiteral("line_type")).toString(QStringLiteral("solidLine")));
    }

    if (method == QLatin1String("add_spline"))
    {
        if (!params.contains(QStringLiteral("draft_block_name")) || !params.contains(QStringLiteral("first_point")) ||
            !params.contains(QStringLiteral("second_point")))
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QStringLiteral("add_spline needs \"draft_block_name\", \"first_point\", \"second_point\"."));
        }
        return m_host->addSpline(params.value(QStringLiteral("draft_block_name")).toString(),
                                  params.value(QStringLiteral("first_point")).toString(),
                                  params.value(QStringLiteral("second_point")).toString(),
                                  params.value(QStringLiteral("angle1")).toString(QStringLiteral("0")),
                                  params.value(QStringLiteral("length1")).toString(QStringLiteral("1")),
                                  params.value(QStringLiteral("angle2")).toString(QStringLiteral("0")),
                                  params.value(QStringLiteral("length2")).toString(QStringLiteral("1")));
    }

    if (method == QLatin1String("add_arc"))
    {
        if (!params.contains(QStringLiteral("draft_block_name")) || !params.contains(QStringLiteral("center_point")) ||
            !params.contains(QStringLiteral("radius")) || !params.contains(QStringLiteral("angle1")) ||
            !params.contains(QStringLiteral("angle2")))
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QStringLiteral("add_arc needs \"draft_block_name\", \"center_point\", \"radius\", "
                                              "\"angle1\", \"angle2\"."));
        }
        return m_host->addArc(params.value(QStringLiteral("draft_block_name")).toString(),
                               params.value(QStringLiteral("center_point")).toString(),
                               params.value(QStringLiteral("radius")).toString(),
                               params.value(QStringLiteral("angle1")).toString(),
                               params.value(QStringLiteral("angle2")).toString());
    }

    if (method == QLatin1String("list_pieces"))
    {
        if (!params.contains(QStringLiteral("draft_block_name")))
        {
            throw RibbenError(RibbenError::InvalidParams, QStringLiteral("list_pieces needs \"draft_block_name\"."));
        }
        QJsonObject result;
        result.insert(QStringLiteral("pieces"),
                      m_host->listPieces(params.value(QStringLiteral("draft_block_name")).toString()));
        return result;
    }

    if (method == QLatin1String("add_piece"))
    {
        if (!params.contains(QStringLiteral("draft_block_name")) || !params.contains(QStringLiteral("name")) ||
            !params.contains(QStringLiteral("outline")))
        {
            throw RibbenError(RibbenError::InvalidParams,
                               QStringLiteral("add_piece needs \"draft_block_name\", \"name\", \"outline\"."));
        }
        const QJsonValue outlineValue = params.value(QStringLiteral("outline"));
        if (!outlineValue.isArray())
        {
            throw RibbenError(RibbenError::InvalidParams, QStringLiteral("\"outline\" must be an array."));
        }
        return m_host->addPiece(params.value(QStringLiteral("draft_block_name")).toString(),
                                 params.value(QStringLiteral("name")).toString(),
                                 outlineValue.toArray(),
                                 params.value(QStringLiteral("seam_allowance")).toBool(true),
                                 params.value(QStringLiteral("seam_allowance_width")).toString(QStringLiteral("1")));
    }

    throw RibbenError(RibbenError::MethodNotFound, QStringLiteral("Unknown method \"%1\".").arg(method));
}
