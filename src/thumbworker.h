#pragma once

#include <QImage>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>

// Generates the filmstrip thumbnails off the UI thread.
class ThumbWorker : public QThread {
    Q_OBJECT

public:
    ThumbWorker(QString path, double duration, int count, QObject *parent = nullptr)
        : QThread(parent), m_path(std::move(path)), m_duration(duration), m_count(count) {}

    void requestStop();

signals:
    void thumbReady(int index, const QImage &image);

protected:
    void run() override;

private:
    QString m_path;
    double m_duration;
    int m_count;
    std::shared_ptr<std::atomic<bool>> m_cancel = std::make_shared<std::atomic<bool>>(false);
};
