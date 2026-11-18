#include "BaichuanRecording.h"

#include <algorithm>
#include <iterator>

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

namespace {

QByteArray timeXml(const char *tag, const QDate &day, int h, int m, int sec)
{
    return QByteArray("<") + tag + ">\n<year>" + QByteArray::number(day.year()) + "</year>\n<month>"
        + QByteArray::number(day.month()) + "</month>\n<day>" + QByteArray::number(day.day())
        + "</day>\n<hour>" + QByteArray::number(h) + "</hour>\n<minute>" + QByteArray::number(m)
        + "</minute>\n<second>" + QByteArray::number(sec) + "</second>\n</" + tag + ">\n";
}

} // namespace

QByteArray alarmSearchOpenXml(int channel, const QByteArray &uid, const QDate &day)
{
    // streamType 0 = main stream. Every type the firmware knows is asked for;
    // a type it doesn't support is simply never returned.
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<body>\n<findAlarmVideo version=\"1.1\">\n"
           "<channelId>" + QByteArray::number(channel) + "</channelId>\n"
           "<uid>" + uid + "</uid>\n"
           "<logicChnBitmap>255</logicChnBitmap>\n"
           "<streamType>0</streamType>\n"
           "<notSearchVideo>0</notSearchVideo>\n"
           + timeXml("startTime", day, 0, 0, 0) + timeXml("endTime", day, 23, 59, 59)
           + "<alarmType>md, pir, io, people, face, vehicle, dog_cat, visitor, other, package, cry, "
             "crossline, intrusion, loitering, legacy, loss</alarmType>\n"
             "</findAlarmVideo>\n</body>";
}

QByteArray alarmSearchFetchXml(int channel, const QByteArray &handle)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n<body>\n<findAlarmVideo version=\"1.1\">\n"
           "<channelId>" + QByteArray::number(channel) + "</channelId>\n"
           "<fileHandle>" + handle + "</fileHandle>\n"
           "</findAlarmVideo>\n</body>";
}

QByteArray alarmSearchHandle(const QByteArray &openReply)
{
    const int start = openReply.indexOf("<fileHandle>");
    const int end = openReply.indexOf("</fileHandle>", start);
    if (start < 0 || end < 0)
        return {};
    return openReply.mid(start + 12, end - start - 12).trimmed();
}

QString alarmKind(const QString &deviceType)
{
    const QString t = deviceType.trimmed().toLower();
    if (t == QLatin1String("people") || t == QLatin1String("face"))
        return QStringLiteral("person");
    if (t == QLatin1String("vehicle"))
        return QStringLiteral("vehicle");
    if (t == QLatin1String("dog_cat"))
        return QStringLiteral("animal");
    // "other" is the firmware's catch-all motion trigger (reolink_aio files it
    // under motion too).
    if (t == QLatin1String("md") || t == QLatin1String("pir") || t == QLatin1String("other"))
        return QStringLiteral("motion");
    return QStringLiteral("other"); // visitor, package, io, crossline, intrusion, …
}

AlarmPage parseAlarmPage(const QByteArray &xml)
{
    AlarmPage page;
    if (xml.isEmpty()) {
        page.error = QStringLiteral("empty reply");
        return page;
    }
    QXmlStreamReader reader(xml);
    AlarmClip clip;
    bool inClip = false;
    QString timeTag; // "startTime" / "endTime" while inside one
    int parts[6] = {0, 0, 0, 0, 0, 0};
    bool sawInfo = false;
    const auto finishTime = [&]() {
        const QDateTime t(QDate(parts[0], parts[1], parts[2]), QTime(parts[3], parts[4], parts[5]));
        if (timeTag == QLatin1String("startTime"))
            clip.start = t;
        else
            clip.end = t;
    };
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const auto name = reader.name();
            if (name == QLatin1String("alarmVideoInfo")) {
                sawInfo = true;
            } else if (name == QLatin1String("alarmVideo")) {
                clip = AlarmClip();
                inClip = true;
            } else if (inClip && (name == QLatin1String("startTime") || name == QLatin1String("endTime"))) {
                timeTag = name.toString();
                std::fill(std::begin(parts), std::end(parts), 0);
            } else if (!timeTag.isEmpty()) {
                static const char *const keys[6] = {"year", "month", "day", "hour", "minute", "second"};
                for (int i = 0; i < 6; ++i) {
                    if (name == QLatin1String(keys[i])) {
                        // readElementText() moves the reader; `name` is a view
                        // into its buffer, so stop comparing after this.
                        parts[i] = reader.readElementText().trimmed().toInt();
                        break;
                    }
                }
            } else if (inClip && name == QLatin1String("alarmType")) {
                for (const QString &word : reader.readElementText().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    const QString kind = alarmKind(word);
                    if (!clip.kinds.contains(kind))
                        clip.kinds.append(kind);
                }
            } else if (name == QLatin1String("bFinished")) {
                page.finished = reader.readElementText().trimmed() == QLatin1String("1");
            }
        } else if (reader.isEndElement()) {
            const auto name = reader.name();
            if (!timeTag.isEmpty() && name == timeTag) {
                finishTime();
                timeTag.clear();
            } else if (inClip && name == QLatin1String("alarmVideo")) {
                inClip = false;
                if (clip.start.isValid() && !clip.kinds.isEmpty()) {
                    if (!clip.end.isValid() || clip.end < clip.start)
                        clip.end = clip.start;
                    page.clips.append(clip);
                }
            }
        }
    }
    if (reader.hasError()) {
        page.error = reader.errorString();
        return page;
    }
    // A reply without the info element is the device saying there is nothing
    // (more) to page through.
    if (!sawInfo)
        page.finished = true;
    page.ok = true;
    return page;
}

} // namespace rl::bcrecording