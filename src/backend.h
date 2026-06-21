#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include "ffmpeg.h"

class ThumbProvider;
class FilePicker;
class ThumbWorker;

// The bridge between QML and the ffmpeg/ffprobe layer. Holds the currently
// loaded video's info and drives thumbnail generation and export.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY infoChanged)
    Q_PROPERTY(double duration READ duration NOTIFY infoChanged)
    Q_PROPERTY(int thumbCount READ thumbCount NOTIFY thumbsChanged)
    Q_PROPERTY(int thumbReadyCount READ thumbReadyCount NOTIFY thumbsChanged)
    Q_PROPERTY(int thumbRevision READ thumbRevision NOTIFY thumbsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit Backend(ThumbProvider *provider, QObject *parent = nullptr);
    explicit Backend(ThumbProvider *provider, FilePicker *filePicker,
                     QObject *parent = nullptr);
    ~Backend() override;

    QUrl source() const { return m_source; }
    double duration() const { return m_info.duration; }
    int thumbCount() const { return m_thumbCount; }
    int thumbReadyCount() const { return m_thumbReadyCount; }
    int thumbRevision() const { return m_thumbRevision; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }

    // Load a video (probes it, then kicks off thumbnail generation).
    Q_INVOKABLE bool load(const QUrl &url);

    // Open native desktop file dialogs.
    Q_INVOKABLE void openVideoDialog();
    Q_INVOKABLE void exportDialog(double start, double end);

    // Suggested "<name>_trimmed.mp4" target next to the source.
    Q_INVOKABLE QUrl suggestedExportUrl() const;

    // Write [start, end] (seconds) of the loaded video to dst.
    Q_INVOKABLE void exportClip(const QUrl &dst, double start, double end);

signals:
    void infoChanged();
    void thumbsChanged();
    void busyChanged();
    void statusChanged();
    void exportDone(const QString &path);
    void exportFailed(const QString &message);
    void loadError(const QString &message);

private:
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void startThumbs();
    void stopThumbs();
    void revealNextThumb();
    void wireFilePicker();

    ThumbProvider *m_provider;
    FilePicker *m_filePicker;
    ThumbWorker *m_thumbWorker = nullptr;
    ffmpeg::VideoInfo m_info;
    QString m_path;
    QUrl m_source;
    int m_thumbCount = 0;
    int m_thumbAvailableCount = 0;
    int m_thumbReadyCount = 0;
    int m_thumbRevision = 0;
    bool m_thumbWorkerDone = false;
    bool m_busy = false;
    QString m_status;
    QTimer m_thumbRevealTimer;
};
