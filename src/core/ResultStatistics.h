#pragma once

#include "IssueRecord.h"

#include <string>
#include <vector>

namespace gisqc {

struct ResultStatistics {
    int totalRules{0};
    int passedRules{0};
    int warningCount{0};
    int errorCount{0};
    std::string passRateText{"0.0%"};

    static ResultStatistics fromIssues(int totalRules, const std::vector<IssueRecord>& issues);
};

} // namespace gisqc
