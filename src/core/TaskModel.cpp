#include "TaskModel.h"

#include <chrono>
#include <sstream>

namespace gisqc {

namespace {
std::string nextTaskId() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::ostringstream out;
    out << "TASK-" << millis;
    return out.str();
}
} // namespace

TaskModel TaskModel::create(std::string name, std::string inputPath) {
    return TaskModel(nextTaskId(), std::move(name), std::move(inputPath));
}

TaskModel::TaskModel(std::string id, std::string name, std::string inputPath)
    : id_(std::move(id)), name_(std::move(name)), inputPath_(std::move(inputPath)) {}

const std::string& TaskModel::id() const { return id_; }
const std::string& TaskModel::name() const { return name_; }
const std::string& TaskModel::inputPath() const { return inputPath_; }
TaskStatus TaskModel::status() const { return status_; }
int TaskModel::progress() const { return progress_; }
const std::string& TaskModel::errorMessage() const { return errorMessage_; }

void TaskModel::start() {
    status_ = TaskStatus::Scanning;
    progress_ = 5;
    errorMessage_.clear();
}

void TaskModel::markReady() {
    status_ = TaskStatus::Ready;
    progress_ = 20;
    errorMessage_.clear();
}

void TaskModel::runChecking() {
    status_ = TaskStatus::Checking;
    progress_ = 50;
    errorMessage_.clear();
}

void TaskModel::complete() {
    status_ = TaskStatus::Completed;
    progress_ = 100;
    errorMessage_.clear();
}

void TaskModel::fail(std::string message) {
    status_ = TaskStatus::Failed;
    errorMessage_ = std::move(message);
}

void TaskModel::cancel() {
    status_ = TaskStatus::Cancelled;
}

const char* toString(TaskStatus status) {
    switch (status) {
    case TaskStatus::Draft:
        return "draft";
    case TaskStatus::Scanning:
        return "scanning";
    case TaskStatus::Ready:
        return "ready";
    case TaskStatus::Checking:
        return "checking";
    case TaskStatus::Completed:
        return "completed";
    case TaskStatus::Failed:
        return "failed";
    case TaskStatus::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

} // namespace gisqc
