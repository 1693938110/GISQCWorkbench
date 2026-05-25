#pragma once

#include <string>

namespace gisqc {

enum class TaskStatus {
    Draft,
    Scanning,
    Ready,
    Checking,
    Completed,
    Failed,
    Cancelled
};

class TaskModel {
public:
    static TaskModel create(std::string name, std::string inputPath);

    const std::string& id() const;
    const std::string& name() const;
    const std::string& inputPath() const;
    TaskStatus status() const;
    int progress() const;
    const std::string& errorMessage() const;

    void start();
    void markReady();
    void runChecking();
    void complete();
    void fail(std::string message);
    void cancel();

private:
    TaskModel(std::string id, std::string name, std::string inputPath);

    std::string id_;
    std::string name_;
    std::string inputPath_;
    TaskStatus status_{TaskStatus::Draft};
    int progress_{0};
    std::string errorMessage_;
};

const char* toString(TaskStatus status);

} // namespace gisqc
