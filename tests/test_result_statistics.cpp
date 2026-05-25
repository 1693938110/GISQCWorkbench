#include "../src/core/ResultStatistics.h"

#include <cassert>
#include <iostream>

static void testComputesIssueStatistics() {
    std::vector<gisqc::IssueRecord> issues = {
        {"P-0001", "C030201", "GXDX_GD", "1", "线重叠", gisqc::Severity::Error, "待处理"},
        {"P-0002", "C020201", "GXDX_GD", "2", "碎线", gisqc::Severity::Warning, "待处理"},
        {"P-0003", "B010204", "GXDL_GD", "3", "值域错误", gisqc::Severity::Warning, "已确认"},
    };

    auto stats = gisqc::ResultStatistics::fromIssues(10, issues);

    assert(stats.totalRules == 10);
    assert(stats.errorCount == 1);
    assert(stats.warningCount == 2);
    assert(stats.passedRules == 7);
    assert(stats.passRateText == "70.0%");
}

static void testHandlesZeroRules() {
    auto stats = gisqc::ResultStatistics::fromIssues(0, {});

    assert(stats.totalRules == 0);
    assert(stats.passedRules == 0);
    assert(stats.passRateText == "0.0%");
}

int main() {
    testComputesIssueStatistics();
    testHandlesZeroRules();
    std::cout << "ResultStatistics tests passed\n";
    return 0;
}
