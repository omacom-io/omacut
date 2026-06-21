#include "portalfilepicker.h"

#include <QDBusConnection>
#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>

namespace {
struct PortalFilterRule {
    uint type;
    QString pattern;
};

using PortalFilterRules = QList<PortalFilterRule>;

struct PortalFileFilter {
    QString name;
    PortalFilterRules rules;
};

using PortalFileFilters = QList<PortalFileFilter>;

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterRule &rule) {
    argument.beginStructure();
    argument << rule.type << rule.pattern;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterRule &rule) {
    argument.beginStructure();
    argument >> rule.type >> rule.pattern;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFileFilter &filter) {
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFileFilter &filter) {
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

void registerPortalFilterTypes() {
    static const bool registered = [] {
        qDBusRegisterMetaType<PortalFilterRule>();
        qDBusRegisterMetaType<PortalFilterRules>();
        qDBusRegisterMetaType<PortalFileFilter>();
        qDBusRegisterMetaType<PortalFileFilters>();
        return true;
    }();
    Q_UNUSED(registered);
}

PortalFileFilter videoFilter() {
    return {
        QStringLiteral("Video files"),
        {
            {1, QStringLiteral("video/*")},
            {0, QStringLiteral("*.avi")},
            {0, QStringLiteral("*.m4v")},
            {0, QStringLiteral("*.mkv")},
            {0, QStringLiteral("*.mov")},
            {0, QStringLiteral("*.mp4")},
            {0, QStringLiteral("*.mpeg")},
            {0, QStringLiteral("*.mpg")},
            {0, QStringLiteral("*.webm")},
        },
    };
}

PortalFileFilters videoFilters() {
    registerPortalFilterTypes();
    return {
        videoFilter(),
        {QStringLiteral("All files"), {{0, QStringLiteral("*")}}},
    };
}

PortalFileFilter mp4Filter() {
    registerPortalFilterTypes();
    return {QStringLiteral("MP4 video"), {{0, QStringLiteral("*.mp4")}}};
}

PortalFileFilters mp4Filters() {
    return {mp4Filter()};
}

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
    options.insert(QStringLiteral("filters"), QVariant::fromValue(videoFilters()));
    options.insert(QStringLiteral("current_filter"), QVariant::fromValue(videoFilter()));

    requestFile(QStringLiteral("OpenFile"), QStringLiteral("Open Video File"), options, Action::Open);
}

void PortalFilePicker::exportVideo(const QUrl &suggestedUrl, double start, double end) {
    const QFileInfo target(suggestedUrl.toLocalFile());

    QVariantMap options;
    options.insert(QStringLiteral("handle_token"), portalToken());
    options.insert(QStringLiteral("accept_label"), QStringLiteral("Export"));
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("current_folder"), portalPathBytes(target.absolutePath()));
    options.insert(QStringLiteral("current_name"), target.fileName());
    options.insert(QStringLiteral("filters"), QVariant::fromValue(mp4Filters()));
    options.insert(QStringLiteral("current_filter"), QVariant::fromValue(mp4Filter()));

    if (requestFile(QStringLiteral("SaveFile"), QStringLiteral("Save Video File"),
                    options, Action::Export)) {
        m_pendingExportStart = start;
        m_pendingExportEnd = end;
    }
}

bool PortalFilePicker::requestFile(const QString &method, const QString &title,
                                   const QVariantMap &options, Action action) {
    if (m_pendingAction != Action::None)
        return false;

    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.FileChooser"),
                          QDBusConnection::sessionBus());
    if (!portal.isValid()) {
        emit failed(QStringLiteral("The XDG desktop portal file chooser is not available."));
        return false;
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
    return true;
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
