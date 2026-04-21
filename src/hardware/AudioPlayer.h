#pragma once
#include <QObject>
#include "app/Config.h"
class QMediaPlayer;
class QAudioOutput;
class AudioPlayer : public QObject {
    Q_OBJECT
public:
    explicit AudioPlayer(const Config& cfg, QObject* parent = nullptr);
    void play(const QString& sound_key);
    void playTTS(const QString& text);
    void setVolume(int vol_percent);
private:
    const Config& cfg_;
    QMediaPlayer*  player_ = nullptr;
    QAudioOutput*  output_ = nullptr;
};
