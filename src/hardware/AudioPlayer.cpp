#include "hardware/AudioPlayer.h"

#include <QFileInfo>
#include <QStringList>
#include <QDebug>

AudioPlayer::AudioPlayer(const Config& cfg, QObject* parent)
    : QObject(parent), cfg_(cfg)
{
    if (!cfg_.device.has_audio) {
        qInfo() << "[Audio] disabled via config";
        return;
    }

    // One-shot watchdog timer: kill aplay if it hasn't finished within
    // kPlayTimeoutMs. Prevents the queue from stalling when ALSA is locked.
    play_timeout_ = new QTimer(this);
    play_timeout_->setSingleShot(true);
    play_timeout_->setInterval(kPlayTimeoutMs);
    connect(play_timeout_, &QTimer::timeout, this, [this]() {
        qWarning() << "[Audio] aplay timeout — killing hung process";
        killCurrent();
    });

    setVolume(cfg_.audio.volume);
}

void AudioPlayer::play(const QString& sound_key, AudioChannel channel) {
    if (!cfg_.device.has_audio) return;

    const QString path = cfg_.audio.soundPath(sound_key);
    if (!QFileInfo::exists(path)) {
        qDebug() << "[Audio] skip (no file):" << path;
        return;
    }

    QString device;
    switch (channel) {
        case AudioChannel::Left:    device = cfg_.audio.device_left;    break;
        case AudioChannel::Right:   device = cfg_.audio.device_right;   break;
        case AudioChannel::Default: device = cfg_.audio.device_default; break;
    }

    queue_.enqueue({path, device});

    if (!current_)
        startNext();
}

void AudioPlayer::startNext() {
    if (queue_.isEmpty()) return;

    const SoundRequest req = queue_.dequeue();

    QStringList args;
    args << "-q";
    if (!req.device.isEmpty()) args << "-D" << req.device;
    args << req.path;

    current_ = new QProcess(this);
    connect(current_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        if (play_timeout_) play_timeout_->stop();
        current_->deleteLater();
        current_ = nullptr;
        startNext();
    });
    current_->start("aplay", args);
    if (play_timeout_) play_timeout_->start();
}

void AudioPlayer::killCurrent() {
    if (!current_) return;
    current_->kill();
    current_->waitForFinished(500);
    current_->deleteLater();
    current_ = nullptr;
    startNext();
}

void AudioPlayer::playTTS(const QString& text) {
    qInfo() << "[Audio][TTS]" << text;
}

void AudioPlayer::setVolume(int vol_percent) {
    if (!cfg_.device.has_audio) return;
    const int v = qBound(0, vol_percent, 100);
    // Non-blocking: start amixer and forget — volume change is best-effort.
    auto* proc = new QProcess(this);
    proc->connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                  proc, &QProcess::deleteLater);
    proc->start("amixer", {"-q", "sset", "Headphone",
                            QString::number(v) + "%"});
}
