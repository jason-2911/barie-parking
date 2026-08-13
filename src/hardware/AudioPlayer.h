#pragma once
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QTimer>
#include "app/Config.h"
#include "types/MessageTypes.h"

// Plays WAV cues via `aplay` in a serial queue — each sound starts only after
// the previous one finishes. Routing to ALSA virtual devices (parking_left /
// parking_right / default) keeps hw:0,0 shared via dmix.
class AudioPlayer : public QObject {
    Q_OBJECT
public:
    explicit AudioPlayer(const Config& cfg, QObject* parent = nullptr);

    void play(const QString& sound_key,
              AudioChannel channel = AudioChannel::Default);

    void playTTS(const QString& text);
    void setVolume(int vol_percent);

private:
    static constexpr int kPlayTimeoutMs = 10000;   // kill aplay if it hangs > 10 s

    struct SoundRequest {
        QString path;
        QString device;
    };

    void startNext();
    void killCurrent();

    const Config&        cfg_;
    QProcess*            current_       = nullptr;
    QTimer*              play_timeout_  = nullptr;
    QQueue<SoundRequest> queue_;
};
