#pragma once

#include "../core/TaskSession.h"

#include <QObject>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

namespace gisqc {

class QualityCheckWorker : public QObject {
    Q_OBJECT
public:
    explicit QualityCheckWorker(QObject* parent = nullptr);

    void setTaskName(const std::string& name) { taskName_ = name; }
    void setInputPath(const std::string& path) { inputPath_ = path; }
    void setRules(const std::vector<RuleDefinition>& rules) { rules_ = rules; }
    void setGlobalTolerance(const std::string& tolerance) { globalTolerance_ = tolerance; }

    void requestCancel() { cancelled_.store(true); resume(); }
    bool isCancelled() const { return cancelled_.load(); }

    void requestPause() { paused_.store(true); }
    void resume() { paused_.store(false); cv_.notify_all(); }
    bool isPaused() const { return paused_.load(); }

public slots:
    void process();

signals:
    void progressChanged(int percent, const QString& message);
    void finished(const TaskSessionReport& report);
    void errorOccurred(const QString& errorMessage);
    void cancelled();

private:
    std::string taskName_;
    std::string inputPath_;
    std::string globalTolerance_;
    std::vector<RuleDefinition> rules_;
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> paused_{false};
    std::mutex pauseMutex_;
    std::condition_variable cv_;

    void waitIfPaused();
};

} // namespace gisqc
