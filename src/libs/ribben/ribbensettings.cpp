#include "ribbensettings.h"

#include <QRandomGenerator>
#include <QSettings>

namespace
{
    const char *KEY_ENABLED     = "Ribben/enabled";
    const char *KEY_PORT        = "Ribben/port";
    const char *KEY_ALLOWED_IPS = "Ribben/allowedIps";
    const char *KEY_TOKEN       = "Ribben/token";

    // Same organization/application Seamly2D itself uses for VSettings
    // (see VSettings construction in tmainwindow.cpp / application_2d.cpp),
    // so this reads and writes the same ini file rather than a new one.
    QSettings BackingStore()
    {
        return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("Seamly2DTeam"),
                          QStringLiteral("Seamly2D"));
    }
}

RibbenSettings::RibbenSettings()
{}

bool RibbenSettings::isEnabled() const
{
    return BackingStore().value(KEY_ENABLED, false).toBool();
}

void RibbenSettings::setEnabled(bool enabled)
{
    QSettings settings = BackingStore();
    settings.setValue(KEY_ENABLED, enabled);
}

quint16 RibbenSettings::port() const
{
    return static_cast<quint16>(BackingStore().value(KEY_PORT, defaultPort()).toUInt());
}

void RibbenSettings::setPort(quint16 port)
{
    QSettings settings = BackingStore();
    settings.setValue(KEY_PORT, port);
}

QStringList RibbenSettings::allowedIps() const
{
    return BackingStore().value(KEY_ALLOWED_IPS, defaultAllowedIps()).toStringList();
}

void RibbenSettings::setAllowedIps(const QStringList &allowedIps)
{
    QSettings settings = BackingStore();
    settings.setValue(KEY_ALLOWED_IPS, allowedIps);
}

QString RibbenSettings::token()
{
    QSettings settings = BackingStore();
    QString existing = settings.value(KEY_TOKEN).toString();
    if (!existing.isEmpty())
    {
        return existing;
    }

    const QString generated = generateToken();
    settings.setValue(KEY_TOKEN, generated);
    return generated;
}

quint16 RibbenSettings::defaultPort()
{
    return 51230;
}

QStringList RibbenSettings::defaultAllowedIps()
{
    return {QStringLiteral("127.0.0.1"), QStringLiteral("::1")};
}

QString RibbenSettings::generateToken()
{
    // 32 hex chars (128 bits) from Qt's cryptographically secure RNG.
    QString token;
    token.reserve(32);
    for (int i = 0; i < 32; ++i)
    {
        token.append(QString::number(QRandomGenerator::system()->bounded(16), 16));
    }
    return token;
}
