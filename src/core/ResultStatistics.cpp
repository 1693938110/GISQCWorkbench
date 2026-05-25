#include "ResultStatistics.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace gisqc {

ResultStatistics ResultStatistics::fromIssues(int totalRules, const std::vector<IssueRecord>& issues) {
    ResultStatistics stats;
    stats.totalRules = std::max(0, totalRules);

    for (const auto& issue : issues) {
        if (issue.severity == Severity::Error) {
            ++stats.errorCount;
        } else if (issue.severity == Severity::Warning) {
            ++stats.warningCount;
        }
    }

    const int failedRuleLikeCount = stats.errorCount + stats.warningCount;
    stats.passedRules = std::max(0, stats.totalRules - failedRuleLikeCount);

    const double passRate = stats.totalRules == 0 ? 0.0 : (static_cast<double>(stats.passedRules) * 100.0 / stats.totalRules);
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << passRate << "%";
    stats.passRateText = out.str();
    return stats;
}

} // namespace gisqc
