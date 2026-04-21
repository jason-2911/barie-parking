#include "hardware/AudioPlayer.h"

#include <QMediaPlayer>
#include <QAudioOutput>
#include <QUrl>
#include <QFileInfo>
#include <QDebug>

AudioPlayer::AudioPlayer(const Config& cfg, QObject* parent)
    : QObject(parent), cfg_(cfg)
{
    if (!cfg_.device.has_audio) {
        qInfo() << "[Audio] disabled via config";
        return;
    }
    player_ = new QMediaPlayer(this);
    output_ = new QAudioOutput(this);
    player_->setAudioOutput(output_);
    output_->setVolume(cfg_.audio.volume / 100.0f);

    // Log backend errors but don't crash — missing GStreamer plugins are non-fatal.
    connect(player_, &QMediaPlayer::errorOccurred,
            this, [](QMediaPlayer::Error, const QString& str) {
        qWarning() << "[Audio] backend error:" << str;
    });
}

void AudioPlayer::play(const QString& sound_key) {
    if (!player_) return;
    const QString path = cfg_.audio.soundPath(sound_key);
    if (!QFileInfo::exists(path)) {
        qDebug() << "[Audio] skip (no file):" << path;
        return;
    }
    player_->setSource(QUrl::fromLocalFile(path));
    player_->play();
}

void AudioPlayer::playTTS(const QString& text) {
#ifdef __APPLE__
    // macOS has a built-in `say` CLI — optional; just log by default.
    qInfo() << "[Audio][TTS]" << text;
#else
    qInfo() << "[Audio][TTS]" << text;
    // Production: QProcess::startDetached("espeak", {"-v", "vi", text});
#endif
}

void AudioPlayer::setVolume(int vol_percent) {
    if (output_) output_->setVolume(static_cast<float>(vol_percent) / 100.0f);
}
