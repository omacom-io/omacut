#include "ffmpeg.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace ffmpeg {

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
    proc.waitForFinished(-1);

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

QImage thumbnail(const QString &path, double time, int height) {
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
    proc.waitForFinished(-1);

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};

    const QByteArray data = proc.readAllStandardOutput();
    QImage img;
    img.loadFromData(data, "JPEG");
    return img;
}

QStringList trimArgs(const QString &src, const QString &dst, double start, double end, bool lossless) {
    QStringList args = {"-y", "-loglevel", "error"};
    // -ss before -i seeks fast; -t gives the output duration.
    args << "-ss" << QString::number(start, 'f', 3)
         << "-i" << src
         << "-t" << QString::number(qMax(end - start, 0.0), 'f', 3);
    
    if (lossless) {
        // Stream copy: no re-encoding, fastest but may not be frame-accurate
        args << "-c" << "copy";
    } else {
        // Re-encode with libx264/aac for frame-accuracy
        args << "-c:v" << "libx264" << "-preset" << "veryfast"
             << "-crf" << "18" << "-c:a" << "aac";
    }
    
    args << dst;
    return args;
}

}  // namespace ffmpeg
