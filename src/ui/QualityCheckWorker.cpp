#include "QualityCheckWorker.h"

#include "../core/DatasetScanService.h"
#include "../core/RuleCheckEngine.h"
#include "../core/RuleTemplateStore.h"
#include "../core/TaskHistoryStore.h"

#include <chrono>
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

std::string formatElapsed(double seconds) {
    if (seconds < 1.0) return std::to_string(static_cast<int>(seconds * 1000)) + " ms";
    if (seconds < 60.0) return std::to_string(static_cast<int>(seconds)) + " s";
    const int mins = static_cast<int>(seconds) / 60;
    const int secs = static_cast<int>(seconds) % 60;
    return std::to_string(mins) + "m " + std::to_string(secs) + "s";
}

} // namespace

QualityCheckWorker::QualityCheckWorker(QObject* parent) : QObject(parent) {}

void QualityCheckWorker::process() {
    const auto startTime = std::chrono::steady_clock::now();
    try {
        TaskSessionReport report{TaskModel::create(taskName_, inputPath_), {}, {}, {}, {}};
        report.logs.push_back("创建质检任务：" + taskName_);

        emit progressChanged(5, QString::fromUtf8("检查成果路径..."));

        if (!std::filesystem::exists(pathFromUtf8(inputPath_))) {
            report.task.fail("成果路径不存在：" + inputPath_);
            report.logs.push_back(report.task.errorMessage());
            TaskHistoryStore{}.append(report);
            emit errorOccurred(QString::fromUtf8(("成果路径不存在：" + inputPath_).c_str()));
            return;
        }

        if (cancelled_.load()) { emit cancelled(); return; }

        report.task.start();
        emit progressChanged(10, QString::fromUtf8("开始扫描成果目录..."));
        report.logs.push_back("开始扫描成果目录：" + inputPath_);

        DatasetScanService scanService;
        report.scan = scanService.scan(inputPath_);
        report.logs.insert(report.logs.end(), report.scan.logs.begin(), report.scan.logs.end());
        report.task.markReady();
        report.logs.push_back("扫描完成，发现数据源 " + std::to_string(report.scan.sourceCount) + " 个");

        if (cancelled_.load()) { emit cancelled(); return; }

        emit progressChanged(30, QString::fromUtf8("扫描完成，开始执行规则检查..."));

        report.task.runChecking();
        std::vector<RuleDefinition> effectiveRules = rules_;
        if (!globalTolerance_.empty()) {
            for (auto& rule : effectiveRules) {
                if (rule.parameters.find("tolerance") == rule.parameters.end()) {
                    rule.parameters["tolerance"] = globalTolerance_;
                }
            }
            report.logs.push_back("应用全局容差：" + globalTolerance_ + "（未单独配置 tolerance 的规则）");
        }
        const int totalRules = static_cast<int>(effectiveRules.size());
        report.logs.push_back("开始执行规则检查，启用规则 " + std::to_string(totalRules) + " 条");

        // Execute rules one-by-one for cancel checks and per-rule progress
        RuleCheckEngine engine;
        for (int ri = 0; ri < totalRules; ++ri) {
            if (cancelled_.load()) { emit cancelled(); return; }

            const int pct = 30 + (ri * 55) / std::max(totalRules, 1);
            const auto& ruleCode = effectiveRules[static_cast<std::size_t>(ri)].code;
            emit progressChanged(pct, QString::fromUtf8(
                ("执行规则 " + std::to_string(ri + 1) + "/" + std::to_string(totalRules) + "：" + ruleCode).c_str()));

            std::vector<RuleDefinition> singleRule{effectiveRules[static_cast<std::size_t>(ri)]};
            auto ruleIssues = engine.check(inputPath_, singleRule);
            report.issues.insert(report.issues.end(), ruleIssues.begin(), ruleIssues.end());
        }

        if (cancelled_.load()) { emit cancelled(); return; }

        emit progressChanged(90, QString::fromUtf8("规则检查完成，生成统计报告..."));

        report.statistics = ResultStatistics::fromIssues(totalRules, report.issues);
        report.task.complete();

        const auto endTime = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(endTime - startTime).count();
        report.logs.push_back("质检完成，发现问题 " + std::to_string(report.issues.size()) + " 个，耗时 " + formatElapsed(elapsed));
        TaskHistoryStore{}.append(report);

        emit progressChanged(100, QString::fromUtf8("质检完成。"));
        emit finished(report);

    } catch (const std::exception& ex) {
        emit errorOccurred(QString::fromUtf8(ex.what()));
    }
}

} // namespace gisqc
