#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <optional>

namespace rl::bcrecording {

struct Segment {
    QDateTime start;
    QDateTime end;
    QDateTime playbackStart;
    QString type;
    QString name;
};

struct SearchResult {
    bool ok = false;
    QString error;
    QList<Segment> segments;
};

// Baichuan timestamps are camera wall-clock components. Keep the conversion in
// this helper so camera/host clock offsets are applied consistently.
QDateTime parseTimestamp(const QString &value);
qint64 wallClockToEpoch(const QDateTime &cameraTime, qint64 cameraOffsetSecs = 0);
QDateTime epochToCameraTime(qint64 epoch, qint64 cameraOffsetSecs = 0);
SearchResult parseSearch(const QByteArray &xml);

// Detection clips from the device's own index (Baichuan cmd 272 opens a search,
// 273 fetches a page of up to ~60 clips, 274 closes it). Each clip is one
// detection with its own start/end and the device's type words (md, people,
// vehicle, dog_cat…). This is what the official apps mark the timeline from;
// the request layout follows MIT reolink_aio's search_vod_type, and the reply
// shape was checked against an RLN8-410.
struct AlarmClip {
    QDateTime start; // device wall-clock
    QDateTime end;
    QStringList kinds; // person | vehicle | animal | motion | other, deduplicated
};

struct AlarmPage {
    bool ok = false;
    bool finished = false;
    QString error;
    QList<AlarmClip> clips;
};

QByteArray alarmSearchOpenXml(int channel, const QByteArray &uid, const QDate &day);
QByteArray alarmSearchFetchXml(int channel, const QByteArray &handle);
// The open reply's <fileHandle>; empty when the device refused the search.
QByteArray alarmSearchHandle(const QByteArray &openReply);
AlarmPage parseAlarmPage(const QByteArray &xml);
// One device type word to the timeline's kind.
QString alarmKind(const QString &deviceType);

} // namespace rl::bcrecording