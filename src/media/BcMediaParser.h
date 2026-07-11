#pragma once

#include <QByteArray>

#include <functional>

// Reassembles a Baichuan (BC) media byte stream into H.264/H.265 Annex-B video.
//
// The binary payloads of the video messages form one continuous "BCMedia" stream,
// independent of BC message boundaries: a single video frame (an I-frame can be
// ~190 KB) spans many messages, and one message can hold several small frames.
// Feed each message's already-decrypted media bytes to append(); the parser
// re-splits the rolling buffer by BCMedia framing and emits video NAL data (which
// is already Annex-B with 00 00 00 01 start codes — SPS/PPS/VPS inline in every
// I-frame). Info frames update the declared width/height. Audio frames are
// currently skipped.
//
// Clean-room from the documented BCMedia frame layout (a factual wire format),
// not from AGPL neolink.
namespace rl {

class BcMediaParser
{
public:
    enum class Codec { Unknown, H264, H265 };

    struct VideoFrame {
        bool keyFrame = false;
        Codec codec = Codec::Unknown;
        quint32 microseconds = 0; // presentation time source
        quint32 time = 0;         // POSIX UTC wall-clock of the frame (0 if absent)
        QByteArray annexB;        // raw elementary-stream bytes, feed to a decoder
    };

    // Called for each complete video frame, in stream order.
    std::function<void(const VideoFrame &)> onVideo;

    // Append decrypted media bytes from one BC message and parse any now-complete
    // frames (invoking onVideo). Returns the number of video frames emitted.
    int append(const QByteArray &mediaBytes);

    int declaredWidth() const { return m_width; }
    int declaredHeight() const { return m_height; }

    // Discard the rolling buffer (e.g. on seek, so a partial frame from the old
    // position isn't spliced onto the new stream).
    void reset() { m_buf.clear(); }

private:
    QByteArray m_buf;
    int m_width = 0;
    int m_height = 0;
};

} // namespace rl
