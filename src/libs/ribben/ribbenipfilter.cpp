#include "ribbenipfilter.h"

namespace
{
    QList<QPair<QHostAddress, int>> ParseEntries(const QStringList &allowedIps)
    {
        QList<QPair<QHostAddress, int>> subnets;
        for (const QString &rawEntry : allowedIps)
        {
            const QString entry = rawEntry.trimmed();
            if (entry.isEmpty())
            {
                continue;
            }

            const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(entry);
            if (!subnet.first.isNull() && subnet.second >= 0)
            {
                subnets.append(subnet);
            }
        }
        return subnets;
    }
}

RibbenIpFilter::RibbenIpFilter(const QStringList &allowedIps)
    : m_subnets(ParseEntries(allowedIps))
{}

bool RibbenIpFilter::isAllowed(const QHostAddress &address) const
{
    for (const auto &subnet : m_subnets)
    {
        if (address.isInSubnet(subnet))
        {
            return true;
        }
    }

    // An IPv4 client connecting via an IPv4-mapped IPv6 address (::ffff:a.b.c.d,
    // which Qt's QTcpServer can hand back on dual-stack sockets) needs the
    // IPv4 form to match a plain "127.0.0.1" allow-list entry.
    bool convertedOk = false;
    const quint32 v4 = address.toIPv4Address(&convertedOk);
    if (convertedOk)
    {
        const QHostAddress normalized(v4);
        for (const auto &subnet : m_subnets)
        {
            if (normalized.isInSubnet(subnet))
            {
                return true;
            }
        }
    }

    return false;
}

QStringList RibbenIpFilter::invalidEntries(const QString &allowedIpsCsv)
{
    QStringList invalid;
    const QStringList entries = allowedIpsCsv.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &rawEntry : entries)
    {
        const QString entry = rawEntry.trimmed();
        if (entry.isEmpty())
        {
            continue;
        }

        const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(entry);
        if (subnet.first.isNull() || subnet.second < 0)
        {
            invalid.append(entry);
        }
    }
    return invalid;
}
