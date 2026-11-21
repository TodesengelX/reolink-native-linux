#pragma once

#include "protocol/BaichuanClient.h"

#include <QSize>
#include <QString>

#include <atomic>
#include <functional>

namespace rl {

// Saves a stretch of recorded footage to an MP4 by pulling it over native
// Baichuan (TCP 9000) by-time playback, unpaced, and stream-copying the frames
// into the file: no decode, no re-encode. Playback already continues across the
// device's recording files, so one export can span many of them (up to a day).
//
// Video only. Timestamps come from the frames' own microsecond clock, so the
// file keeps the camera's real frame timing; gaps between recordings are closed
// up rather than padded.
class ClipExporter
{
public:
    struct Request {
        BaichuanClient::Params params; // params.startEpoch = where playback starts
        qint64 startEpoch = 0;         // requested range, wall-clock seconds
        qint64 endEpoch = 0;
        QString path;                  // output .mp4
        QSize declaredSize;            // fallback if the bitstream doesn't say
    };
    struct Result {
        bool ok = false;
        bool cancelled = false;
        bool endedEarly = false;       // the device stopped sending before endEpoch
        qint64 frames = 0;
        qint64 bytes = 0;
        double mediaSecs = 0;
        QString error;
    };

    // Blocking; run it on a worker thread. `progress` gets 0..1 from that
    // thread, a few times a second at most. Setting `cancel` stops the pull and
    // keeps what was saved so far.
    static Result run(const Request &req, const std::atomic<bool> &cancel,
                      const std::function<void(double)> &progress);
};

} // namespace rl
