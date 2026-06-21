#include "backend.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include "filepicker.h"
#include "portalfilepicker.h"
#include "thumbprovider.h"
#include "thumbworker.h"

namespace {
constexpr int kThumbCount = 12;
}

Backend::Backend(ThumbProvider *provider, QObject *parent)
    : Backend(provider, new PortalFilePicker(), parent) {}

Backend::Backend(ThumbProvider *provider, FilePicker *filePicker, QObject *parent)
    : QObject(parent), m_provider(provider), m_filePicker(filePicker) {
    if (!m_filePicker->parent())
        m_filePicker->setParent(this);
    wireFilePicker();
}

void Backend::wireFilePicker() {
    connect(m_filePicker, &FilePicker::openSelected, this, &Backend::load);
    connect(m_filePicker, &FilePicker::exportSelected, this, &Backend::exportClip);
    connect(m_filePicker, &FilePicker::failed, this, &Backend::loadError);
}

void Backend::setBusy(bool busy) {
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void Backend::setStatus(const QString &status) {
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

bool Backend::load(const QUrl &url) {
    const QString path = url.toLocalFile();
    const ffmpeg::VideoInfo info = ffmpeg::probe(path);
    if (!info.ok) {
        emit loadError(info.error);
        return false;
    }

    m_info = info;
    m_path = path;
    m_source = url;

    // New video: drop the old filmstrip and bump the revision so QML reloads.
    m_thumbCount = 0;
    ++m_thumbRevision;
    m_provider->setImages({});
    emit thumbsChanged();

    emit infoChanged();

    setStatus(QStringLiteral("Loading..."));
    startThumbs();
    return true;
}

void Backend::openVideoDialog() {
    m_filePicker->openVideo();
}

void Backend::exportDialog(double start, double end) {
    if (m_path.isEmpty() || !m_info.ok)
        return;

    m_filePicker->exportVideo(suggestedExportUrl(), start, end);
}

void Backend::startThumbs() {
    auto *worker = new ThumbWorker(m_path, m_info.duration, kThumbCount, this);
    connect(worker, &ThumbWorker::ready, this, [this, worker](const QVector<QImage> &images) {
        m_provider->setImages(images);
        m_thumbCount = images.size();
        ++m_thumbRevision;
        emit thumbsChanged();
        setStatus(QString());
        worker->deleteLater();
    });
    connect(worker, &ThumbWorker::finished, worker, [worker] {
        // Safety net if `ready` was never delivered.
        if (worker->parent())
            worker->deleteLater();
    });
    worker->start();
}

QUrl Backend::sourceFolder() const {
    if (m_path.isEmpty())
        return {};
    return QUrl::fromLocalFile(QFileInfo(m_path).absolutePath());
}

QUrl Backend::suggestedExportUrl() const {
    if (m_path.isEmpty())
        return {};
    const QFileInfo src(m_path);
    const QString target =
        src.dir().filePath(src.completeBaseName() + "_trimmed." + src.suffix());
    return QUrl::fromLocalFile(target);
}

void Backend::exportClip(const QUrl &dst, double start, double end) {
    if (m_path.isEmpty() || !m_info.ok)
        return;

    const QString outPath = dst.toLocalFile();
    const QString ffmpegBin = ffmpeg::toolPath("ffmpeg");
    if (ffmpegBin.isEmpty()) {
        emit exportFailed("`ffmpeg` was not found on your PATH.");
        return;
    }

    setBusy(true);
    setStatus(QStringLiteral("Exporting…"));

    const QStringList args = ffmpeg::trimArgs(m_path, outPath, start, end);

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::finished, this,
            [this, proc, outPath](int code, QProcess::ExitStatus exitStatus) {
                const QString err = QString::fromUtf8(proc->readAll()).trimmed();
                proc->deleteLater();
                setBusy(false);
                if (exitStatus == QProcess::NormalExit && code == 0) {
                    setStatus(QString());
                    emit exportDone(outPath);
                } else {
                    setStatus(QString());
                    emit exportFailed(err.isEmpty() ? "ffmpeg trim failed." : err);
                }
            });
    proc->start(ffmpegBin, args);
}
