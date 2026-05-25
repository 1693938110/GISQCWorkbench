#include "TaskHistoryStore.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace gisqc {

namespace fs = std::filesystem;

void TaskHistoryStore::append(const TaskSessionReport& report) const {
    const auto path = fs::path(historyPath());
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }
    const bool needsHeader = !fs::exists(path) || fs::file_size(path) == 0;
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        return;
    }
    if (needsHeader) {
        out << "timestamp\ttaskName\tinputPath\tsourceCount\tissueCount\tpassRate\tstatus\n";
    }

    const auto record = fromReport(report);
    out << escapeField(record.timestamp) << '\t'
        << escapeField(record.taskName) << '\t'
        << escapeField(record.inputPath) << '\t'
        << record.sourceCount << '\t'
        << record.issueCount << '\t'
        << escapeField(record.passRateText) << '\t'
        << escapeField(record.status) << '\n';
}

std::vector<TaskHistoryRecord> TaskHistoryStore::latest(std::size_t limit) const {
    std::ifstream in(historyPath(), std::ios::binary);
    if (!in) {
        return {};
    }

    std::vector<TaskHistoryRecord> records;
    std::string line;
    bool firstLine = true;
    while (std::getline(in, line)) {
        if (firstLine) {
            firstLine = false;
            if (line.rfind("timestamp\t", 0) == 0) {
                continue;
            }
        }
        const auto fields = splitLine(line);
        if (fields.size() < 7) {
            continue;
        }
        TaskHistoryRecord record;
        record.timestamp = unescapeField(fields[0]);
        record.taskName = unescapeField(fields[1]);
        record.inputPath = unescapeField(fields[2]);
        try {
            record.sourceCount = std::stoi(fields[3]);
            record.issueCount = std::stoi(fields[4]);
        } catch (...) {
            record.sourceCount = 0;
            record.issueCount = 0;
        }
        record.passRateText = unescapeField(fields[5]);
        record.status = unescapeField(fields[6]);
        records.push_back(std::move(record));
    }

    std::reverse(records.begin(), records.end());
    if (records.size() > limit) {
        records.resize(limit);
    }
    return records;
}

std::vector<TaskHistoryRecord> TaskHistoryStore::all() const {
    std::ifstream in(historyPath(), std::ios::binary);
    if (!in) {
        return {};
    }

    std::vector<TaskHistoryRecord> records;
    std::string line;
    bool firstLine = true;
    while (std::getline(in, line)) {
        if (firstLine) {
            firstLine = false;
            if (line.rfind("timestamp\t", 0) == 0) {
                continue;
            }
        }
        const auto fields = splitLine(line);
        if (fields.size() < 7) {
            continue;
        }
        TaskHistoryRecord record;
        record.timestamp = unescapeField(fields[0]);
        record.taskName = unescapeField(fields[1]);
        record.inputPath = unescapeField(fields[2]);
        try {
            record.sourceCount = std::stoi(fields[3]);
            record.issueCount = std::stoi(fields[4]);
        } catch (...) {
            record.sourceCount = 0;
            record.issueCount = 0;
        }
        record.passRateText = unescapeField(fields[5]);
        record.status = unescapeField(fields[6]);
        records.push_back(std::move(record));
    }

    std::reverse(records.begin(), records.end());
    return records;
}

std::string TaskHistoryStore::historyPath() const {
    return (fs::current_path() / "data" / "history" / "task_history.tsv").string();
}

TaskHistoryRecord TaskHistoryStore::fromReport(const TaskSessionReport& report) {
    TaskHistoryRecord record;
    record.timestamp = nowText();
    record.taskName = report.task.name();
    record.inputPath = report.task.inputPath();
    record.sourceCount = report.scan.sourceCount;
    record.issueCount = static_cast<int>(report.issues.size());
    record.passRateText = report.statistics.passRateText;
    record.status = toString(report.task.status());
    return record;
}

std::string TaskHistoryStore::nowText() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string TaskHistoryStore::escapeField(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '\t':
            escaped += "\\t";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string TaskHistoryStore::unescapeField(const std::string& value) {
    std::string unescaped;
    unescaped.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            unescaped.push_back(value[i]);
            continue;
        }
        const char next = value[++i];
        switch (next) {
        case 't':
            unescaped.push_back('\t');
            break;
        case 'n':
            unescaped.push_back('\n');
            break;
        case 'r':
            unescaped.push_back('\r');
            break;
        case '\\':
            unescaped.push_back('\\');
            break;
        default:
            unescaped.push_back(next);
            break;
        }
    }
    return unescaped;
}

std::vector<std::string> TaskHistoryStore::splitLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

} // namespace gisqc
