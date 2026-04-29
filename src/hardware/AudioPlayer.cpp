#include "hardware/AudioPlayer.h"

#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QDebug>

AudioPlayer::AudioPlayer(const Config& cfg, QObject* parent)
    : QObject(parent), cfg_(cfg)
{
    if (!cfg_.device.has_audio) {
        qInfo() << "[Audio] disabled via config";
        return;
    }
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

    QStringList args;
    args << "-q";
    if (!device.isEmpty()) args << "-D" << device;
    args << path;

    if (!QProcess::startDetached("aplay", args)) {
        qWarning() << "[Audio] aplay startDetached failed:"
                   << "device=" << (device.isEmpty() ? "<default>" : device)
                   << "path="   << path;
    }
}

void AudioPlayer::playTTS(const QString& text) {
    qInfo() << "[Audio][TTS]" << text;
    // Production: QProcess::startDetached("espeak", {"-v", "vi", text});
}

void AudioPlayer::setVolume(int vol_percent) {
    if (!cfg_.device.has_audio) return;
    const int v = qBound(0, vol_percent, 100);
    // Best-effort: set ALSA Master to the requested level. Failures are
    // non-fatal — board may not have a "Master" mixer control.
    QProcess::execute("amixer", {"-q", "sset", "Master",
                                  QString::number(v) + "%"});
}
