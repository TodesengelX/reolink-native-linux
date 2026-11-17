#pragma once

#include <QDateTime>
#include <QString>

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

} // namespace rl::bcrecording