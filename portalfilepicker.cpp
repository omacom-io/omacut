#include "portalfilepicker.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>

namespace {
QString portalToken() {
    return QStringLiteral("omacut_%1").arg(QRandomGenerator::global()->generate());
}

QByteArray portalPathBytes(const QString &path) {
    QByteArray bytes = path.toUtf8();
    bytes.append('\0');
    return bytes;
}
}

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) {}

void PortalFilePicker::openVideo() {
    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), portalToken());
    options.insert(QStringLiteral("accept_label"), QStringLiteral("Open"));
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("multiple"), false);
    options.insert(QStringLiteral("current_folder"), portalPathBytes(QDir::homePath()));

    requestFile(QStringLiteral("OpenFile"), QStringLiteral("Open video"), options, Action::Open);
}

void PortalFilePicker::exportVideo(const QUrl &suggestedUrl, double start, double end) {
    const QFileInfo target(suggestedUrl.toLocalFile());

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), portalToken());
    options.insert(QStringLiteral("accept_label"), QStringLiteral("Export"));
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("current_folder"), portalPathBytes(target.absolutePath()));
    options.insert(QStringLiteral("current_name"), target.fileName());

    m_pendingExportStart = start;
    m_pendingExportEnd = end;
    requestFile(QStringLiteral("SaveFile"), QStringLiteral("Export trimmed video"),
                options, Action::Export);
}

void PortalFilePicker::requestFile(const QString &method, const QString &title,
                                   const QVariantMap &options, Action action) {
    if (!m_pendingPath.isEmpty())
        return;

    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.FileChooser"),
                          QDBusConnection::sessionBus());
    if (!portal.isValid()) {
        emit failed(QStringLiteral("The XDG desktop portal file chooser is not available."));
        return;
    }

    m_pendingAction = action;
    auto *watcher = new QDBusPendingCallWatcher(
        portal.asyncCall(method, QString(), title, options), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher]() {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();

        if (reply.isError()) {
            clearPending();
            emit failed(QStringLiteral("The portal file picker failed: %1")
                            .arg(reply.error().message()));
            return;
        }

        m_pendingPath = reply.value().path();
        const bool connected = QDBusConnection::sessionBus().connect(
            QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
            this, SLOT(handleResponse(uint,QVariantMap)));
        if (!connected) {
            clearPending();
            emit failed(QStringLiteral("Could not listen for the portal file picker response."));
        }
    });
}

void PortalFilePicker::handleResponse(uint response, const QVariantMap &results) {
    const Action action = m_pendingAction;
    const double start = m_pendingExportStart;
    const double end = m_pendingExportEnd;
    clearPending();

    if (response != 0)
        return;

    const QStringList uris = results.value(QStringLiteral("uris")).toStringList();
    if (uris.isEmpty())
        return;

    const QUrl url(uris.first());
    if (action == Action::Open)
        emit openSelected(url);
    else if (action == Action::Export)
        emit exportSelected(url, start, end);
}

void PortalFilePicker::clearPending() {
    if (!m_pendingPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"),
            this, SLOT(handleResponse(uint,QVariantMap)));
    }

    m_pendingPath.clear();
    m_pendingAction = Action::None;
}
