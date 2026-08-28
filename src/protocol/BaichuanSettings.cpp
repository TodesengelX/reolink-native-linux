#include "BaichuanSettings.h"

#include <QList>
#include <QXmlStreamReader>

namespace rl::bcsettings {
namespace {

// Command ids and XML leaves are from the existing native settings path and
// observed Baichuan replies. No AGPL neolink implementation is used here.
const QList<Schema> kSchemas = {
    {26, 25, QStringLiteral("image"), {}, ChannelScope::Channel,
     {QStringLiteral("bright"), QStringLiteral("contrast"), QStringLiteral("saturation"),
      QStringLiteral("sharpen"), QStringLiteral("mirror"), QStringLiteral("flip"),
      QStringLiteral("DayNight/mode"), QStringLiteral("DayNight/Threshold")}, true},
    {46, 47, QStringLiteral("motion"), {}, ChannelScope::Channel,
     {QStringLiteral("enable"), QStringLiteral("sensitivity"), QStringLiteral("valueTable")},
     true},
    {54, 55, QStringLiteral("recording"), {}, ChannelScope::Channel,
     {QStringLiteral("cycle"), QStringLiteral("preRecordTime"),
      QStringLiteral("recordDelayTime")}, true},
    {81, 82, QStringLiteral("recordingSchedule"), {}, ChannelScope::Channel,
     {QStringLiteral("enable"), QStringLiteral("type"), QStringLiteral("valueTable")}, true},
    {342, 343, QStringLiteral("aiDetection"), {}, ChannelScope::Channel,
     {QStringLiteral("enable"), QStringLiteral("sensitivity"), QStringLiteral("stayTime")}, true},
    {80, 0, QStringLiteral("deviceInfo"), {}, ChannelScope::Host,
     {QStringLiteral("name"), QStringLiteral("type"), QStringLiteral("channelNum"),
      QStringLiteral("firmVer"), QStringLiteral("hardVer")}, true},
};

} // namespace

const Schema *schemaFor(quint32 getCommand)
{
    for (const Schema &schema : kSchemas)
        if (schema.getCommand == getCommand)
            return &schema;
    return nullptr;
}

Result parse(const QByteArray &xml, quint16 status, const Schema &schema)
{
    Result result;
    result.status = status;
    if (xml.isEmpty()) {
        result.error = QStringLiteral("empty Baichuan reply");
        result.transport = true;
        return result;
    }

    QXmlStreamReader reader(xml);
    QStringList stack;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            stack.append(reader.name().toString());
        } else if (token == QXmlStreamReader::EndElement) {
            if (!stack.isEmpty())
                stack.removeLast();
        } else if (token == QXmlStreamReader::Characters && !reader.isWhitespace()
                   && !stack.isEmpty()) {
            const QString leaf = stack.last();
            const QString value = reader.text().toString();
            if (!result.fields.contains(leaf))
                result.fields.insert(leaf, value);
            if (stack.size() >= 2) {
                const QString path = stack.at(stack.size() - 2) + QLatin1Char('/') + leaf;
                if (!result.fields.contains(path))
                    result.fields.insert(path, value);
            }
        }
    }
    if (reader.hasError()) {
        result.protocol = true;
        result.error = reader.errorString();
        return result;
    }
    for (auto it = result.fields.cbegin(); it != result.fields.cend(); ++it) {
        if (!schema.responseTags.contains(it.key()))
            result.unknownFields.insert(it.key(), it.value());
    }
    result.ok = status == 200 || status == 201 || status == 300;
    result.protocol = !result.ok;
    if (!result.ok)
        result.error = QStringLiteral("device rejected command (status %1)").arg(status);
    return result;
}

bool validateChanges(quint32 getCommand, const QVariantMap &changes, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
        const QString key = it.key();
        const QVariant value = it.value();
        if (value.userType() == QMetaType::QString && value.toString().isEmpty())
            return fail(QStringLiteral("%1 cannot be empty").arg(key));

        bool numeric = false;
        const int number = value.toInt(&numeric);
        if (getCommand == 26) {
            if (key == QLatin1String("DayNight/mode")) {
                if (value.toString() != QLatin1String("auto")
                    && value.toString() != QLatin1String("color")
                    && value.toString() != QLatin1String("blackAndWhite"))
                    return fail(QStringLiteral("invalid day/night mode"));
            } else if (key == QLatin1String("DayNight/Threshold")) {
                if (value.toString() != QLatin1String("low")
                    && value.toString() != QLatin1String("medium")
                    && value.toString() != QLatin1String("high"))
                    return fail(QStringLiteral("invalid day/night threshold"));
            } else if (!numeric || number < 0 || number > 255) {
                return fail(QStringLiteral("%1 must be between 0 and 255").arg(key));
            }
        } else if (getCommand == 46) {
            if (key == QLatin1String("valueTable"))
                continue;
            if (key.endsWith(QLatin1String("//sensitivity"))) {
                if (!numeric || number < 1 || number > 50)
                    return fail(QStringLiteral("%1 must be between 1 and 50").arg(key));
                continue;
            }
            if (!numeric || number < 1 || number > 50)
                return fail(QStringLiteral("%1 must be between 1 and 50").arg(key));
        } else if (getCommand == 342) {
            if (!numeric || number < 0 || number > 100)
                return fail(QStringLiteral("%1 must be between 0 and 100").arg(key));
        } else if (getCommand == 54) {
            if (key == QLatin1String("cycle")) {
                if (!numeric || (number != 0 && number != 1))
                    return fail(QStringLiteral("cycle must be 0 or 1"));
            } else if (!numeric || number < 0 || number > 300) {
                return fail(QStringLiteral("%1 must be between 0 and 300").arg(key));
            }
        } else {
            return fail(QStringLiteral("unsupported Baichuan settings command %1").arg(getCommand));
        }
    }
    return true;
}

} // namespace rl::bcsettings