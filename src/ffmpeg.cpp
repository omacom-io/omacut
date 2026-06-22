#include "ffmpeg.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace ffmpeg {

namespace {
// Upper bound on a synchronous probe so a hung/unresponsive file (e.g. a stalled
// network mount) can't freeze the caller — load() runs probe() on the UI thread.
constexpr int kProbeTimeoutMs = 15000;
// Poll granularity while waiting on a thumbnail child, so cancellation is prompt.
constexpr int kThumbPollMs = 50;
}

QString toolPath(const QString &tool) {
    return QStandardPaths::findExecutable(tool);
}

VideoInfo probe(const QString &path) {
    VideoInfo info;
    info.path = path;

    const QString ffprobe = toolPath("ffprobe");
    if (ffprobe.isEmpty()) {
        info.error = "`ffprobe` was not found on your PATH. Install ffmpeg.";
        return info;
    }

    QProcess proc;
    proc.start(ffprobe, {
        "-v", "error",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        "-select_streams", "v:0",
        path,
    });
    if (!proc.waitForFinished(kProbeTimeoutMs)) {
        proc.kill();
        proc.waitForFinished(-1);
        info.error = "ffprobe timed out reading this file.";
        return info;
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        info.error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        if (info.error.isEmpty())
            info.error = "ffprobe failed.";
        return info;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    const QJsonObject root = doc.object();
    const QJsonArray streams = root.value("streams").toArray();
    if (streams.isEmpty()) {
        info.error = "No video stream found in this file.";
        return info;
    }

    const QJsonObject stream = streams.first().toObject();

    // Duration can live on the stream or on the container.
    QString durationStr = stream.value("duration").toString();
    if (durationStr.isEmpty())
        durationStr = root.value("format").toObject().value("duration").toString();
    if (durationStr.isEmpty()) {
        info.error = "Could not determine the video duration.";
        return info;
    }

    info.duration = durationStr.toDouble();

    info.ok = info.duration > 0.0;
    if (!info.ok)
        info.error = "Video has a zero or invalid duration.";
    return info;
}

QImage thumbnail(const QString &path, double time, int height,
                 const std::atomic<bool> *cancel) {
    const QString ffmpeg = toolPath("ffmpeg");
    if (ffmpeg.isEmpty())
        return {};

    QProcess proc;
    proc.start(ffmpeg, {
        "-loglevel", "error",
        "-ss", QString::number(qMax(time, 0.0), 'f', 3),
        "-i", path,
        "-frames:v", "1",
        "-vf", QString("scale=-1:%1").arg(height),
        "-f", "image2pipe",
        "-vcodec", "mjpeg",
        "pipe:1",
    });

    // Poll instead of waitForFinished(-1) so a cancel request can kill the child
    // promptly — otherwise the std::future destructor in ThumbWorker would block
    // the UI thread until ffmpeg finishes on its own.
    while (!proc.waitForFinished(kThumbPollMs)) {
        if (proc.state() == QProcess::NotRunning)
            break;  // failed to start, or exited between polls
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            proc.kill();
            proc.waitForFinished(-1);
            return {};
        }
    }

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};

    const QByteArray data = proc.readAllStandardOutput();
    QImage img;
    img.loadFromData(data, "JPEG");
    return img;
}

QStringList trimArgs(const QString &src, const QString &dst, double start, double end) {
    QStringList args = {"-y", "-loglevel", "error"};
    // -ss before -i seeks fast; -t gives the output duration.
    args << "-ss" << QString::number(start, 'f', 3)
         << "-i" << src
         << "-t" << QString::number(qMax(end - start, 0.0), 'f', 3)
         << "-c:v" << "libx264" << "-preset" << "veryfast"
         << "-crf" << "18" << "-c:a" << "aac"
         << dst;
    return args;
}

}  // namespace ffmpeg
