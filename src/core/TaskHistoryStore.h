#pragma once

#include "TaskSession.h"

#include <string>
#include <vector>

namespace gisqc {

struct TaskHistoryRecord {
    std::string timestamp;
    std::string taskName;
    std::string inputPath;
    int sourceCount{0};
    int issueCount{0};
    std::string passRateText;
    std::string status;
};

class TaskHistoryStore {
public:
    void append(const TaskSessionReport& report) const;
    std::vector<TaskHistoryRecord> latest(std::size_t limit) const;
    std::vector<TaskHistoryRecord> all() const;
    std::string historyPath() const;

private:
    static TaskHistoryRecord fromReport(const TaskSessionReport& report);
    static std::string nowText();
    static std::string escapeField(const std::string& value);
    static std::string unescapeField(const std::string& value);
    static std::vector<std::string> splitLine(const std::string& line);
};

} // namespace gisqc
