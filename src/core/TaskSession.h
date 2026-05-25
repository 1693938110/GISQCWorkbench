#pragma once

#include "DatasetScanService.h"
#include "IssueRecord.h"
#include "ResultStatistics.h"
#include "RuleDefinition.h"
#include "TaskModel.h"

#include <string>
#include <vector>

namespace gisqc {

struct TaskSessionReport {
    TaskModel task;
    DatasetScanSummary scan;
    std::vector<IssueRecord> issues;
    ResultStatistics statistics;
    std::vector<std::string> logs;
};

class TaskSession {
public:
    TaskSessionReport run(const std::string& taskName, const std::string& inputPath, const std::vector<RuleDefinition>& rules) const;
    TaskSessionReport run(const std::string& taskName, const std::string& inputPath, const std::vector<RuleDefinition>& rules, const std::string& globalTolerance) const;
};

} // namespace gisqc

#ifdef QT_CORE_LIB
#include <QMetaType>
Q_DECLARE_METATYPE(gisqc::TaskSessionReport)
#endif
