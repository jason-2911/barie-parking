#include "app/DeviceId.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>

namespace {

QString normalize(QString s) {
    s = s.trimmed().toLower();
    s.remove(QRegularExpression("[^0-9a-f]"));
    return s;
}

#ifndef __APPLE__
QString readFirstLine(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream ts(&f);
    return ts.readLine();
}

// Linux: parse "Serial" from /proc/cpuinfo (Raspberry Pi / Orange Pi).
QString readCpuSerial() {
    QFile f("/proc/cpuinfo");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        if (line.startsWith("Serial", Qt::CaseInsensitive)) {
            int colon = line.indexOf(':');
            if (colon > 0) return line.mid(colon + 1).trimmed();
        }
    }
    return {};
}
#endif

QString firstNonLoopbackMac() {
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (flags.testFlag(QNetworkInterface::IsLoopBack))   continue;
        if (!flags.testFlag(QNetworkInterface::IsUp))        continue;
        const QString mac = iface.hardwareAddress();
        if (!mac.isEmpty()) return mac;
    }
    return {};
}

#ifdef __APPLE__
QString readIoPlatformUuid() {
    QProcess p;
    p.start("/usr/sbin/ioreg",
            {"-rd1", "-c", "IOPlatformExpertDevice"});
    if (!p.waitForFinished(2000)) return {};
    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
    QRegularExpression re("\"IOPlatformUUID\"\\s*=\\s*\"([0-9A-Fa-f-]+)\"");
    const auto m = re.match(out);
    return m.hasMatch() ? m.captured(1) : QString{};
}
#endif

}  // anonymous namespace

QString DeviceId::detect(const QString& prefix) {
    QString id;

#ifdef __APPLE__
    id = readIoPlatformUuid();
#else
    id = readFirstLine("/etc/machine-id");
    if (id.isEmpty()) id = readFirstLine("/var/lib/dbus/machine-id");
    if (id.isEmpty()) id = readFirstLine("/sys/class/dmi/id/product_uuid");
    if (id.isEmpty()) id = readCpuSerial();
#endif

    if (id.isEmpty()) id = firstNonLoopbackMac();

    id = normalize(id);
    if (id.isEmpty()) return {};

    // Shorten to 16 hex chars — enough uniqueness for a fleet, short
    // enough to log comfortably and to fit in an MQTT client id.
    if (id.size() > 16) id = id.left(16);

    return prefix.isEmpty() ? id : prefix + id;
}
