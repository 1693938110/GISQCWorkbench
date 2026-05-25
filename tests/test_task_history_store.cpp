#include "../src/core/TaskHistoryStore.h"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static gisqc::TaskSessionReport makeReport(const std::string& name, int issues) {
    gisqc::TaskSessionReport report{gisqc::TaskModel::create(name, "D:/数据/成果\t目录")};
    report.task.complete();
    report.scan.sourceCount = 3;
    report.statistics.totalRules = 10;
    report.statistics.passedRules = 10 - issues;
    report.statistics.errorCount = issues;
    report.statistics.passRateText = issues == 0 ? "100.0%" : "80.0%";
    for (int i = 0; i < issues; ++i) {
        gisqc::IssueRecord issue;
        issue.issueId = "P-" + std::to_string(i + 1);
        issue.ruleCode = "A010103";
        report.issues.push_back(issue);
    }
    return report;
}

int main() {
    const auto original = fs::current_path();
    const auto root = fs::temp_directory_path() / "gis_qc_task_history_store_test";
    fs::remove_all(root);
    fs::create_directories(root);
    fs::current_path(root);

    gisqc::TaskHistoryStore store;
    store.append(makeReport("第一次任务", 2));
    store.append(makeReport("第二次任务", 0));

    const auto records = store.latest(5);
    assert(records.size() == 2);
    assert(records[0].taskName == "第二次任务");
    assert(records[0].issueCount == 0);
    assert(records[0].status == "completed");
    assert(records[1].taskName == "第一次任务");
    assert(records[1].inputPath == "D:/数据/成果\t目录");

    fs::current_path(original);
    fs::remove_all(root);
    std::cout << "TaskHistoryStore tests passed\n";
    return 0;
}
