#include "BaichuanRecording.h"

#include <QList>
#include <QRegularExpression>
#include <QXmlStreamReader>

namespace rl::bcrecording {
namespace {

QString leafText(const QHash<QString, QString> &fields, const QStringList &names)
{
    for (const QString &name : names)
        if (fields.contains(name))
            return fields.value(name);
    return {};
}

QDateTime filenameTimestamp(const QString &filename)
{
    // Reolink recording names begin with a two-character stream/type prefix,
    // followed by YYYYMMDD_HHMMSS. This is the playback clock boundary.
    const QRegularExpression match(QStringLiteral("(?:^|/)..(\\d{8})_(\\d{6})"));
    const auto result = match.match(filename);
    if (!result.hasMatch())
        return {};
    return parseTimestamp(result.captured(1) + result.captured(2));
}

QDateTime componentTimestamp(const QHash<QString, QString> &fields, const QString &prefix)
{
    bool ok = false;
    const int year = fields.value(prefix + "/year").toInt(&ok);
    if (!ok)
        return {};
    const int month = fields.value(prefix + "/month").toInt(&ok);
    const int day = fields.value(prefix + "/day").toInt(&ok);
    const int hour = fields.value(prefix + "/hour").toInt(&ok);
    const int minute = fields.value(prefix + "/minute").toInt(&ok);
    const int second = fields.value(prefix + "/second").toInt(&ok);
    if (!ok)
        return {};
    const QDateTime result(QDate(year, month, day), QTime(hour, minute, second));
    return result.isValid() ? result : QDateTime();
}

QDateTime fieldTimestamp(const QHash<QString, QString> &fields,
                         const QStringList &scalarNames, const QString &componentPrefix)
{
    const QDateTime component = componentTimestamp(fields, componentPrefix);
    return component.isValid() ? component : parseTimestamp(leafText(fields, scalarNames));
}

} // namespace

QDateTime parseTimestamp(const QString &value)
{
    const QString text = value.trimmed();
    const QStringList formats = {QStringLiteral("yyyy-MM-dd hh:mm:ss"),
                                 QStringLiteral("yyyyMMddhhmmss"),
                                 QStringLiteral("yyyy-MM-ddThh:mm:ss")};
    for (const QString &format : formats) {
        const QDateTime result = QDateTime::fromString(text, format);
        if (result.isValid())
            return result;
    }
    return {};
}

qint64 wallClockToEpoch(const QDateTime &cameraTime, qint64 cameraOffsetSecs)
{
    if (!cameraTime.isValid())
        return 0;
    return cameraTime.toSecsSinceEpoch() - cameraOffsetSecs;
}

QDateTime epochToCameraTime(qint64 epoch, qint64 cameraOffsetSecs)
{
    return QDateTime::fromSecsSinceEpoch(epoch + cameraOffsetSecs);
}

SearchResult parseSearch(const QByteArray &xml)
{
    SearchResult result;
    if (xml.isEmpty()) {
        result.error = QStringLiteral("empty Baichuan recording reply");
        return result;
    }

    QXmlStreamReader reader(xml);
    QStringList stack;
    QHash<QString, QString> fields;
    bool inFile = false;
    int fileDepth = 0;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QString tag = reader.name().toString();
            stack.append(tag);
            if (tag == QLatin1String("FileInfo") || tag == QLatin1String("fileInfo")
                || tag == QLatin1String("record") || tag == QLatin1String("Record")
                || tag == QLatin1String("alarmVideo")) {
                if (!inFile)
                    fields.clear();
                inFile = true;
                ++fileDepth;
            } else if (inFile) {
                ++fileDepth;
            }
        } else if (token == QXmlStreamReader::Characters && !reader.isWhitespace()
                   && inFile && !stack.isEmpty()) {
            const QString tag = stack.last();
            if (!fields.contains(tag))
                fields.insert(tag, reader.text().toString().trimmed());
            if (stack.size() >= 2) {
                const QString path = stack.at(stack.size() - 2) + QLatin1Char('/') + tag;
                if (!fields.contains(path))
                    fields.insert(path, reader.text().toString().trimmed());
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QString tag = reader.name().toString();
            if (inFile) {
                --fileDepth;
                const bool endFile = (tag == QLatin1String("FileInfo")
                                      || tag == QLatin1String("fileInfo")
                                      || tag == QLatin1String("record")
                                      || tag == QLatin1String("Record")
                                      || tag == QLatin1String("alarmVideo"))
                                     && fileDepth == 0;
                if (endFile) {
                    const QDateTime start = fieldTimestamp(
                        fields, {QStringLiteral("startTime"), QStringLiteral("start")},
                        QStringLiteral("startTime"));
                    const QDateTime end = fieldTimestamp(
                        fields, {QStringLiteral("endTime"), QStringLiteral("end")},
                        QStringLiteral("endTime"));
                    if (start.isValid() && end.isValid() && end > start) {
                        // Skip search summaries: they have a <handle> but no filename.
                        const QString name = leafText(fields, {QStringLiteral("name"),
                                                              QStringLiteral("fileName")});
                        if (fields.contains(QLatin1String("handle")) && name.isEmpty()) {
                            fields.clear();
                            inFile = false;
                            continue;
                        }

                        Segment segment;
                        segment.start = start;
                        segment.end = end;
                        segment.playbackStart = fieldTimestamp(
                            fields, {QStringLiteral("playbackTime"), QStringLiteral("playbackStart")},
                            QStringLiteral("playbackTime"));
                        segment.type = leafText(fields, {QStringLiteral("recordType"),
                                                          QStringLiteral("type"),
                                                          QStringLiteral("fileType")});
                        segment.name = leafText(fields, {QStringLiteral("cameraName"),
                                                         QStringLiteral("name"),
                                                         QStringLiteral("fileName")});
                        if (!segment.playbackStart.isValid())
                            segment.playbackStart = filenameTimestamp(
                                leafText(fields, {QStringLiteral("fileName")}));
                        
                        // Log the raw segment data
                        // qCInfo(lcProto) is not available here, using printf for now or I can include Log.h
                        // Actually, I'll just return it and let DeviceManager log it.
                        result.segments.append(segment);
                    }
                    fields.clear();
                    inFile = false;
                }
            }
            if (!stack.isEmpty())
                stack.removeLast();
        }
    }
    if (reader.hasError()) {
        result.error = reader.errorString();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace rl::bcrecording