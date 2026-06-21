#pragma once

#include <QVariantMap>

#include "filepicker.h"

class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    void openVideo() override;
    void exportVideo(const QUrl &suggestedUrl, double start, double end) override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    enum class Action {
        None,
        Open,
        Export
    };

    void requestFile(const QString &method, const QString &title,
                     const QVariantMap &options, Action action);
    void clearPending();

    QString m_pendingPath;
    Action m_pendingAction = Action::None;
    double m_pendingExportStart = 0;
    double m_pendingExportEnd = 0;
};
