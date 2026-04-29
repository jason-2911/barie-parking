#pragma once
#include <QObject>
#include "app/Config.h"
#include "types/MessageTypes.h"

// Plays short WAV cues by spawning detached `aplay` processes. The subprocess
// model lets us route concurrent sounds to different ALSA virtual devices
// (parking_left / parking_right) without QMediaPlayer holding hw:0,0
// exclusive — required for the "one stereo jack, two speakers" setup.
class AudioPlayer : public QObject {
    Q_OBJECT
public:
    explicit AudioPlayer(const Config& cfg, QObject* parent = nullptr);

    // Play `sound_key` (looked up via cfg.audio.sounds) on the requested
    // channel. Default = system default ALSA device; Left/Right = the
    // configured parking_left / parking_right virtual devices.
    void play(const QString& sound_key,
              AudioChannel channel = AudioChannel::Default);

    void playTTS(const QString& text);
    void setVolume(int vol_percent);

private:
    const Config& cfg_;
};
