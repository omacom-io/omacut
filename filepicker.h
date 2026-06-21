#pragma once

#include <QObject>
#include <QUrl>

class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openVideo() = 0;
    virtual void exportVideo(const QUrl &suggestedUrl, double start, double end) = 0;

signals:
    void openSelected(const QUrl &url);
    void exportSelected(const QUrl &url, double start, double end);
    void failed(const QString &message);
};
