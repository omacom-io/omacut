#include "thumbworker.h"

#include "ffmpeg.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <future>
#include <memory>

namespace {
constexpr int kMaxThumbJobs = 4;
}

void ThumbWorker::run() {
    if (m_count <= 0)
        return;

    const int idealThreads = std::max(1, QThread::idealThreadCount());
    const int maxJobs = std::min({m_count, kMaxThumbJobs, idealThreads});

    // Shared with every async job so an interruption can kill in-flight ffmpeg
    // children. Set it before returning, otherwise the std::future destructors
    // would block this thread (and thus stopThumbs()) until ffmpeg exits.
    auto cancel = std::make_shared<std::atomic<bool>>(false);

    std::deque<std::future<QImage>> jobs;
    int nextIndex = 0;
    int emitIndex = 0;

    const auto startNextJob = [this, &jobs, &nextIndex, cancel] {
        const int index = nextIndex++;
        const double time = m_duration * (index + 0.5) / m_count;
        const QString path = m_path;
        jobs.push_back(std::async(std::launch::async, [path, time, cancel] {
            return ffmpeg::thumbnail(path, time, 90, cancel.get());
        }));
    };

    while (nextIndex < m_count && static_cast<int>(jobs.size()) < maxJobs)
        startNextJob();

    while (!jobs.empty()) {
        if (isInterruptionRequested()) {
            cancel->store(true, std::memory_order_relaxed);
            return;
        }

        QImage image = jobs.front().get();
        jobs.pop_front();

        if (isInterruptionRequested()) {
            cancel->store(true, std::memory_order_relaxed);
            return;
        }

        emit thumbReady(emitIndex++, image);

        if (nextIndex < m_count)
            startNextJob();
    }
}
