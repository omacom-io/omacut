#include "backend.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <memory>

#include "filepicker.h"
#include "portalfilepicker.h"
#include "thumbprovider.h"
#include "thumbworker.h"

namespace {
constexpr int kThumbCount = 12;
constexpr int kThumbRevealMs = 70;

QString mp4PathFor(const QString &path) {
    const QFileInfo file(path);
    if (file.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0)
        return path;

    const QString baseName = file.completeBaseName().isEmpty()
        ? file.fileName()
        : file.completeBaseName();
    return file.dir().filePath(baseName + QStringLiteral(".mp4"));
}
}

Backend::Backend(ThumbProvider *provider, QObject *parent)
    : Backend(provider, new PortalFilePicker(), parent) {}

Backend::Backend(ThumbProvider *provider, FilePicker *filePicker, QObject *parent)
    : QObject(parent), m_provider(provider), m_filePicker(filePicker) {
    if (!m_filePicker->parent())
        m_filePicker->setParent(this);
    wireFilePicker();
    m_thumbRevealTimer.setInterval(kThumbRevealMs);
    connect(&m_thumbRevealTimer, &QTimer::timeout, this, &Backend::revealNextThumb);
}

Backend::~Backend() {
    stopThumbs();
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
    stopThumbs();
    m_thumbCount = kThumbCount;
    m_thumbAvailableCount = 0;
    m_thumbReadyCount = 0;
    m_thumbWorkerDone = false;
    ++m_thumbRevision;
    m_provider->setImages(QVector<QImage>(kThumbCount));
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
    auto *worker = new ThumbWorker(m_path, m_info.duration, kThumbCount);
    m_thumbWorker = worker;

    connect(worker, &ThumbWorker::thumbReady, this, [this, worker](int index, const QImage &image) {
        if (worker != m_thumbWorker)
            return;
        m_provider->setImage(index, image);
        m_thumbAvailableCount = qMax(m_thumbAvailableCount, index + 1);
        if (m_thumbReadyCount == 0)
            revealNextThumb();
        if (!m_thumbRevealTimer.isActive())
            m_thumbRevealTimer.start();
    });
    connect(worker, &ThumbWorker::finished, this, [this, worker] {
        if (worker == m_thumbWorker) {
            m_thumbWorker = nullptr;
            m_thumbWorkerDone = true;
            if (m_thumbReadyCount >= m_thumbCount)
                setStatus(QString());
            else if (!m_thumbRevealTimer.isActive())
                m_thumbRevealTimer.start();
        }
        worker->deleteLater();
    });
    worker->start();
}

void Backend::revealNextThumb() {
    if (m_thumbReadyCount < m_thumbAvailableCount) {
        ++m_thumbReadyCount;
        emit thumbsChanged();
    }

    if (m_thumbReadyCount < m_thumbAvailableCount)
        return;

    m_thumbRevealTimer.stop();
    if (m_thumbWorkerDone && m_thumbReadyCount >= m_thumbCount)
        setStatus(QString());
}

void Backend::stopThumbs() {
    m_thumbRevealTimer.stop();
    if (!m_thumbWorker)
        return;

    ThumbWorker *worker = m_thumbWorker;
    m_thumbWorker = nullptr;
    worker->disconnect(this);
    worker->requestInterruption();
    worker->wait();
    delete worker;
}

QUrl Backend::suggestedExportUrl() const {
    if (m_path.isEmpty())
        return {};
    const QFileInfo src(m_path);
    const QString target = src.dir().filePath(src.completeBaseName() + "_trimmed.mp4");
    return QUrl::fromLocalFile(target);
}

void Backend::exportClip(const QUrl &dst, double start, double end) {
    if (m_path.isEmpty() || !m_info.ok)
        return;

    const QString outPath = mp4PathFor(dst.toLocalFile());
    const QString ffmpegBin = ffmpeg::toolPath("ffmpeg");
    if (ffmpegBin.isEmpty()) {
        emit exportFailed("`ffmpeg` was not found on your PATH.");
        return;
    }

    setBusy(true);
    setStatus(QStringLiteral("Exporting…"));

    const QStringList args = ffmpeg::trimArgs(m_path, outPath, start, end);

    auto *proc = new QProcess(this);
    auto completed = std::make_shared<bool>(false);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(proc, &QProcess::finished, this,
            [this, proc, outPath, completed](int code, QProcess::ExitStatus exitStatus) {
                if (*completed)
                    return;
                *completed = true;
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
    connect(proc, &QProcess::errorOccurred, this,
            [this, proc, completed](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart || *completed)
                    return;
                *completed = true;
                const QString err = proc->errorString();
                proc->deleteLater();
                setBusy(false);
                setStatus(QString());
                emit exportFailed(err.isEmpty() ? "Could not start ffmpeg." : err);
            });
    proc->start(ffmpegBin, args);
}
