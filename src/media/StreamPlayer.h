#pragma once

#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVideoSink>

#include <atomic>
#include <thread>

namespace rl {

// One live/playback stream: FFmpeg demux + decode on a dedicated worker thread,
// frames delivered to a QML VideoOutput's QVideoSink (NV12/YUV420P upload path —
// DESIGN.md §5: the universal fallback; zero-copy VAAPI arrives with the GPU spike).
//
// Sources: rtsp:// (live, TCP, low-latency flags), or any libavformat-openable
// URL/file (used by tests and the "direct stream" device kind).
class StreamPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QVideoSink *videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    Q_PROPERTY(qint64 framesDecoded READ framesDecoded NOTIFY framesDecodedChanged)
    Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY loopChanged)

public:
    enum class State { Idle, Connecting, Streaming, Error, Stopped };
    Q_ENUM(State)

    explicit StreamPlayer(QObject *parent = nullptr);
    ~StreamPlayer() override;

    QString source() const { return m_source; }
    void setSource(const QString &source);

    QVideoSink *videoSink() const;
    void setVideoSink(QVideoSink *sink);

    State state() const { return m_state; }
    QString errorString() const { return m_errorString; }
    qint64 framesDecoded() const { return m_framesDecoded.load(); }

    bool loop() const { return m_loop; }
    void setLoop(bool loop);

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void sourceChanged();
    void videoSinkChanged();
    void stateChanged();
    void errorStringChanged();
    void framesDecodedChanged();
    void loopChanged();

private:
    void workerLoop();
    // Runs one open→decode session; returns false when the session should not
    // be retried (clean stop / EOF without loop).
    bool runSession(QString *error);
    void setStateFromWorker(State state, const QString &error = {});
    void deliverFrame(const class QVideoFrame &frame);

    friend struct InterruptContext;

    QString m_source;
    mutable QMutex m_sinkMutex;
    QPointer<QVideoSink> m_sink;

    State m_state = State::Idle;
    QString m_errorString;
    std::atomic<qint64> m_framesDecoded{0};
    bool m_loop = false;

    std::thread m_thread;
    std::atomic<bool> m_abort{false};
};

} // namespace rl
