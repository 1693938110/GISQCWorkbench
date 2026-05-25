#include "TaskSession.h"

#include "RuleCheckEngine.h"
#include "TaskHistoryStore.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gisqc {

namespace {

std::filesystem::path pathFromUtf8(const std::string& path) {
#ifdef _WIN32
    if (path.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return std::filesystem::path(path);
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), size);
    wide.resize(static_cast<std::size_t>(size - 1));
    return std::filesystem::path(wide);
#else
    return std::filesystem::u8path(path);
#endif
}

} // namespace

TaskSessionReport TaskSession::run(const std::string& taskName, const std::string& inputPath, const std::vector<RuleDefinition>& rules) const {
    return run(taskName, inputPath, rules, "");
}

TaskSessionReport TaskSession::run(const std::string& taskName, const std::string& inputPath, const std::vector<RuleDefinition>& rules, const std::string& globalTolerance) const {
    TaskSessionReport report{TaskModel::create(taskName, inputPath), {}, {}, {}, {}};
    report.logs.push_back("创建质检任务：" + taskName);

    if (!std::filesystem::exists(pathFromUtf8(inputPath))) {
        report.task.fail("成果路径不存在：" + inputPath);
        report.logs.push_back(report.task.errorMessage());
        TaskHistoryStore{}.append(report);
        return report;
    }

    report.task.start();
    report.logs.push_back("开始扫描成果目录：" + inputPath);

    DatasetScanService scanService;
    report.scan = scanService.scan(inputPath);
    report.logs.insert(report.logs.end(), report.scan.logs.begin(), report.scan.logs.end());
    report.task.markReady();
    report.logs.push_back("扫描完成，发现数据源 " + std::to_string(report.scan.sourceCount) + " 个");

    report.task.runChecking();
    std::vector<RuleDefinition> effectiveRules = rules;
    if (!globalTolerance.empty()) {
        for (auto& rule : effectiveRules) {
            if (rule.parameters.find("tolerance") == rule.parameters.end()) {
                rule.parameters["tolerance"] = globalTolerance;
            }
        }
        report.logs.push_back("应用全局容差：" + globalTolerance + "（未单独配置 tolerance 的规则）");
    }
    report.logs.push_back("开始执行规则检查，启用规则 " + std::to_string(effectiveRules.size()) + " 条");

    RuleCheckEngine engine;
    report.issues = engine.check(inputPath, effectiveRules);
    report.statistics = ResultStatistics::fromIssues(static_cast<int>(effectiveRules.size()), report.issues);
    report.task.complete();
    report.logs.push_back("质检完成，发现问题 " + std::to_string(report.issues.size()) + " 个");
    TaskHistoryStore{}.append(report);

    return report;
}

} // namespace gisqc
