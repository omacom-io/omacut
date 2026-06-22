#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "backend.h"
#include "filepicker.h"
#include "thumbprovider.h"

class FakeFilePicker : public FilePicker {
    Q_OBJECT

public:
    int openCount = 0;
    int exportCount = 0;
    QUrl lastSuggestedUrl;
    double lastStart = 0;
    double lastEnd = 0;

    void openVideo() override { ++openCount; }

    void exportVideo(const QUrl &suggestedUrl, double start, double end) override {
        ++exportCount;
        lastSuggestedUrl = suggestedUrl;
        lastStart = start;
        lastEnd = end;
    }
};

class EnvVarGuard {
public:
    explicit EnvVarGuard(const char *name)
        : m_name(name), m_oldValue(qgetenv(name)), m_hadValue(qEnvironmentVariableIsSet(name)) {}

    ~EnvVarGuard() {
        if (m_hadValue)
            qputenv(m_name.constData(), m_oldValue);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    QByteArray m_oldValue;
    bool m_hadValue;
};

class ShortcutBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY infoChanged)
    Q_PROPERTY(double duration READ duration NOTIFY infoChanged)
    Q_PROPERTY(int thumbCount READ thumbCount NOTIFY thumbsChanged)
    Q_PROPERTY(int thumbReadyCount READ thumbReadyCount NOTIFY thumbsChanged)
    Q_PROPERTY(int thumbRevision READ thumbRevision NOTIFY thumbsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit ShortcutBackend(QUrl source, double duration, QObject *parent = nullptr)
        : QObject(parent), m_source(std::move(source)), m_duration(duration) {}

    QUrl source() const { return m_source; }
    double duration() const { return m_duration; }
    int thumbCount() const { return 0; }
    int thumbReadyCount() const { return 0; }
    int thumbRevision() const { return 0; }
    bool busy() const { return false; }
    QString status() const { return {}; }

    Q_INVOKABLE bool load(const QUrl &) { return false; }
    Q_INVOKABLE void openVideoDialog() { ++openCount; }
    Q_INVOKABLE void exportDialog(double start, double end) {
        ++exportCount;
        lastStart = start;
        lastEnd = end;
    }
    Q_INVOKABLE QUrl suggestedExportUrl() const { return {}; }
    Q_INVOKABLE void exportClip(const QUrl &, double, double) {}

    int openCount = 0;
    int exportCount = 0;
    double lastStart = 0;
    double lastEnd = 0;

signals:
    void infoChanged();
    void thumbsChanged();
    void busyChanged();
    void statusChanged();
    void exportDone(const QString &path);
    void exportFailed(const QString &message);
    void loadError(const QString &message);

private:
    QUrl m_source;
    double m_duration;
};

class BackendTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void openDialogDelegatesToFilePicker();
    void pickerSelectionLoadsVideo();
    void thumbnailSlotsAreExposedImmediately();
    void thumbProviderUsesRevisionPrefixedIds();
    void exportDialogDelegatesSuggestedUrlAndRange();
    void suggestedExportUrlAlwaysUsesMp4();
    void exportClipWritesMp4();
    void exportZeroLengthClipFails();
    void exportStartFailureClearsBusy();
    void failedExportPreservesExistingFile();
    void qmlShortcutsTriggerBackendActions();
    void trimArgsReencodeForPreciseCuts();

private:
    QUrl videoUrl() const { return QUrl::fromLocalFile(m_videoPath); }
    QString formatName(const QString &path) const;
    QString mainQmlPath() const;
    void waitForBackgroundWork(Backend &backend);

    QTemporaryDir m_dir;
    QString m_videoPath;
};

void BackendTests::initTestCase() {
    QVERIFY2(m_dir.isValid(), "temporary directory is valid");
    m_videoPath = m_dir.filePath(QStringLiteral("clip.mp4"));

    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    QVERIFY2(!ffmpeg.isEmpty(), "ffmpeg is available");

    QProcess proc;
    proc.start(ffmpeg, {
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-f"),
        QStringLiteral("lavfi"),
        QStringLiteral("-i"),
        QStringLiteral("testsrc=size=32x32:rate=1:duration=1"),
        QStringLiteral("-pix_fmt"),
        QStringLiteral("yuv420p"),
        QStringLiteral("-y"),
        m_videoPath,
    });
    QVERIFY2(proc.waitForFinished(10000), qPrintable(QString::fromUtf8(proc.readAll())));
    QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
    QCOMPARE(proc.exitCode(), 0);
    QVERIFY(QFileInfo::exists(m_videoPath));
}

void BackendTests::waitForBackgroundWork(Backend &backend) {
    QTRY_VERIFY_WITH_TIMEOUT(backend.status().isEmpty(), 10000);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

QString BackendTests::formatName(const QString &path) const {
    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (ffprobe.isEmpty())
        return {};

    QProcess proc;
    proc.start(ffprobe, {
        QStringLiteral("-v"),
        QStringLiteral("error"),
        QStringLiteral("-show_entries"),
        QStringLiteral("format=format_name"),
        QStringLiteral("-of"),
        QStringLiteral("default=noprint_wrappers=1:nokey=1"),
        path,
    });
    if (!proc.waitForFinished(10000))
        return {};
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        return {};
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

QString BackendTests::mainQmlPath() const {
    return QFileInfo(QString::fromUtf8(__FILE__)).dir().absoluteFilePath(
        QStringLiteral("../src/Main.qml"));
}

void BackendTests::openDialogDelegatesToFilePicker() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);

    backend.openVideoDialog();
    backend.openVideoDialog();

    QCOMPARE(picker->openCount, 2);
}

void BackendTests::pickerSelectionLoadsVideo() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy infoSpy(&backend, &Backend::infoChanged);

    emit picker->openSelected(videoUrl());

    QCOMPARE(infoSpy.count(), 1);
    QCOMPARE(backend.source(), videoUrl());
    QVERIFY(backend.duration() > 0);
    waitForBackgroundWork(backend);
}

void BackendTests::thumbnailSlotsAreExposedImmediately() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy thumbsSpy(&backend, &Backend::thumbsChanged);

    QVERIFY(backend.load(videoUrl()));

    QVERIFY(backend.thumbCount() > 0);
    QCOMPARE(backend.thumbReadyCount(), 0);
    waitForBackgroundWork(backend);
    QCOMPARE(backend.thumbReadyCount(), backend.thumbCount());
    QVERIFY(thumbsSpy.count() > 2);
}

void BackendTests::thumbProviderUsesRevisionPrefixedIds() {
    ThumbProvider provider;
    provider.setImages(QVector<QImage>(2));

    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::red);
    provider.setImage(1, image);

    QSize size;
    QVERIFY(provider.requestImage(QStringLiteral("4/0"), &size, QSize()).isNull());
    QVERIFY(!provider.requestImage(QStringLiteral("4/1"), &size, QSize()).isNull());
    QCOMPARE(size, image.size());
}

void BackendTests::exportDialogDelegatesSuggestedUrlAndRange() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);
    backend.exportDialog(0.25, 0.75);

    QCOMPARE(picker->exportCount, 1);
    QCOMPARE(picker->lastSuggestedUrl,
             QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("clip_trimmed.mp4"))));
    QCOMPARE(picker->lastStart, 0.25);
    QCOMPARE(picker->lastEnd, 0.75);
}

void BackendTests::suggestedExportUrlAlwaysUsesMp4() {
    const QString renamedSource = m_dir.filePath(QStringLiteral("renamed-source.webm"));
    QVERIFY(QFile::copy(m_videoPath, renamedSource));

    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);

    QVERIFY(backend.load(QUrl::fromLocalFile(renamedSource)));
    waitForBackgroundWork(backend);

    QCOMPARE(backend.suggestedExportUrl(),
             QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("renamed-source_trimmed.mp4"))));
}

void BackendTests::exportClipWritesMp4() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy doneSpy(&backend, &Backend::exportDone);
    QSignalSpy failedSpy(&backend, &Backend::exportFailed);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    const QString selectedPath = m_dir.filePath(QStringLiteral("actual-export.webm"));
    const QString mp4Path = m_dir.filePath(QStringLiteral("actual-export.mp4"));
    backend.exportClip(QUrl::fromLocalFile(selectedPath), 0.0, 1.0);

    QVERIFY(backend.busy());
    QTRY_VERIFY_WITH_TIMEOUT(doneSpy.count() + failedSpy.count() > 0, 20000);

    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(doneSpy.count(), 1);
    QCOMPARE(doneSpy.first().at(0).toString(), mp4Path);
    QVERIFY(!backend.busy());
    QVERIFY(QFileInfo::exists(mp4Path));
    QVERIFY(!QFileInfo::exists(selectedPath));
    QVERIFY(ffmpeg::probe(mp4Path).ok);
    QVERIFY2(formatName(mp4Path).contains(QStringLiteral("mp4")),
             qPrintable(formatName(mp4Path)));
}

void BackendTests::exportZeroLengthClipFails() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy doneSpy(&backend, &Backend::exportDone);
    QSignalSpy failedSpy(&backend, &Backend::exportFailed);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    const QString outPath = m_dir.filePath(QStringLiteral("empty-range.mp4"));
    backend.exportClip(QUrl::fromLocalFile(outPath), 0.5, 0.5);

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(doneSpy.count(), 0);
    QVERIFY(!backend.busy());
    QVERIFY(!QFileInfo::exists(outPath));
}

void BackendTests::exportStartFailureClearsBusy() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy failedSpy(&backend, &Backend::exportFailed);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    QTemporaryDir pathDir;
    QVERIFY(pathDir.isValid());
    const QString fakeFfmpeg = pathDir.filePath(QStringLiteral("ffmpeg"));
    QFile fake(fakeFfmpeg);
    QVERIFY(fake.open(QIODevice::WriteOnly | QIODevice::Truncate));
    fake.write("#!/definitely/missing/omacut-ffmpeg\n");
    fake.close();
    QVERIFY(fake.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                | QFileDevice::ExeOwner));

    EnvVarGuard pathGuard("PATH");
    qputenv("PATH", QFile::encodeName(pathDir.path()) + ':' + qgetenv("PATH"));

    backend.exportClip(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("failed.mp4"))),
                       0.0, 1.0);

    QVERIFY(backend.busy());
    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 5000);
    QVERIFY(!backend.busy());
    QVERIFY(backend.status().isEmpty());
    QVERIFY(!failedSpy.first().at(0).toString().isEmpty());
}

void BackendTests::failedExportPreservesExistingFile() {
    ThumbProvider provider;
    auto *picker = new FakeFilePicker;
    Backend backend(&provider, picker);
    QSignalSpy failedSpy(&backend, &Backend::exportFailed);

    QVERIFY(backend.load(videoUrl()));
    waitForBackgroundWork(backend);

    // A pre-existing destination file that a failed export must not clobber.
    const QString outPath = m_dir.filePath(QStringLiteral("keep-me.mp4"));
    const QByteArray original("original contents");
    {
        QFile existing(outPath);
        QVERIFY(existing.open(QIODevice::WriteOnly | QIODevice::Truncate));
        existing.write(original);
        existing.close();
    }

    // Force ffmpeg to fail to start, the same way exportStartFailureClearsBusy does.
    QTemporaryDir pathDir;
    QVERIFY(pathDir.isValid());
    const QString fakeFfmpeg = pathDir.filePath(QStringLiteral("ffmpeg"));
    QFile fake(fakeFfmpeg);
    QVERIFY(fake.open(QIODevice::WriteOnly | QIODevice::Truncate));
    fake.write("#!/definitely/missing/omacut-ffmpeg\n");
    fake.close();
    QVERIFY(fake.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                | QFileDevice::ExeOwner));

    EnvVarGuard pathGuard("PATH");
    qputenv("PATH", QFile::encodeName(pathDir.path()) + ':' + qgetenv("PATH"));

    backend.exportClip(QUrl::fromLocalFile(outPath), 0.0, 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 5000);

    // The original file survives untouched, and no temp part file is left behind.
    QFile check(outPath);
    QVERIFY(check.open(QIODevice::ReadOnly));
    QCOMPARE(check.readAll(), original);
    QVERIFY(!QFileInfo::exists(outPath + QStringLiteral(".omacut-part.mp4")));
}

void BackendTests::qmlShortcutsTriggerBackendActions() {
    QQuickStyle::setStyle(QStringLiteral("Material"));

    ShortcutBackend backend(QUrl::fromLocalFile(m_dir.filePath(QStringLiteral("shortcut-placeholder.mp4"))),
                            1.0);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("thumbs"), new ThumbProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl::fromLocalFile(mainQmlPath()));

    QVERIFY2(!engine.rootObjects().isEmpty(), qPrintable(mainQmlPath()));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);

    window->show();
    window->requestActivate();
    QTest::qWait(100);

    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE_WITH_TIMEOUT(backend.exportCount, 1, 3000);

    QTest::keyClick(window, Qt::Key_Enter);
    QTRY_COMPARE_WITH_TIMEOUT(backend.exportCount, 2, 3000);

    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE_WITH_TIMEOUT(backend.openCount, 1, 3000);
}

void BackendTests::trimArgsReencodeForPreciseCuts() {
    const QStringList args = ffmpeg::trimArgs(QStringLiteral("in.mp4"),
                                              QStringLiteral("out.mp4"),
                                              0.25, 0.75);

    QVERIFY(args.contains(QStringLiteral("libx264")));
    QVERIFY(args.contains(QStringLiteral("aac")));
    QVERIFY(!args.contains(QStringLiteral("copy")));
}

QTEST_MAIN(BackendTests)
#include "backend_tests.moc"
