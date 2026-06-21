#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
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

class BackendTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void openDialogDelegatesToFilePicker();
    void pickerSelectionLoadsVideo();
    void exportDialogDelegatesSuggestedUrlAndRange();
    void trimArgsReencodeForPreciseCuts();

private:
    QUrl videoUrl() const { return QUrl::fromLocalFile(m_videoPath); }
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
    QCOMPARE(backend.videoWidth(), 32);
    QCOMPARE(backend.videoHeight(), 32);
    waitForBackgroundWork(backend);
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
