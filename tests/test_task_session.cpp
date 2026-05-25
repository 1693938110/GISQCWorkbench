#include "core/TaskSession.h"
#include "core/RuleDefinition.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

using namespace gisqc;
namespace fs = std::filesystem;

static RuleDefinition requiredFoldersRule() {
    RuleDefinition rule;
    rule.code = "A010103";
    rule.name = "必交付目录不得缺失";
    rule.category = "成果完整性";
    rule.targetObject = "成果目录";
    rule.enabled = true;
    rule.severity = "error";
    rule.parameters["requiredFolders"] = "空间数据,文档资料";
    rule.message = "缺少必交付子目录";
    return rule;
}

int main() {
    const fs::path root = fs::temp_directory_path() / "gis_qc_task_session_test";
    fs::remove_all(root);
    const fs::path dataset = fs::u8path("D:/jiedan/136/gis-qc-workbench/build-msvc-debug/task_session_dataset");
    fs::remove_all(dataset);
    const fs::path work = root / "work";
    fs::create_directories(dataset / fs::u8path(u8"空间数据"));
    fs::create_directories(work);
    std::ofstream(dataset / fs::u8path(u8"demo.shp")) << "placeholder";

    const auto originalCurrentPath = fs::current_path();
    fs::current_path(work);

    TaskSession session;
    const auto report = session.run("示例任务", dataset.u8string(), {requiredFoldersRule()});

    assert(report.task.name() == "示例任务");
    assert(report.task.inputPath() == dataset.u8string());
    assert(report.task.status() == TaskStatus::Completed);
    assert(report.task.progress() == 100);
    assert(report.scan.sourceCount == 1);
    if (report.issues.size() != 1) {
        std::cerr << "expected 1 issue, got " << report.issues.size() << "\n";
        for (const auto& issue : report.issues) {
            std::cerr << issue.ruleCode << " " << issue.layerName << " " << issue.featureId << " " << issue.description << "\n";
        }
        fs::current_path(originalCurrentPath);
        fs::remove_all(dataset);
        fs::remove_all(root);
        return 2;
    }
    assert(report.issues[0].featureId == "文档资料");
    assert(report.statistics.totalRules == 1);
    assert(report.statistics.errorCount == 1);
    assert(report.logs.size() >= 4);
    assert(report.logs.front().find("创建质检任务") != std::string::npos);
    assert(report.logs.back().find("质检完成") != std::string::npos);

    const auto failed = session.run("坏路径", (dataset / fs::u8path(u8"missing")).u8string(), {requiredFoldersRule()});
    assert(failed.task.status() == TaskStatus::Failed);
    assert(!failed.task.errorMessage().empty());
    assert(failed.scan.sourceCount == 0);
    assert(failed.issues.empty());

    fs::current_path(originalCurrentPath);
    fs::remove_all(dataset);
    fs::remove_all(root);
    std::cout << "TaskSession tests passed\n";
    return 0;
}
