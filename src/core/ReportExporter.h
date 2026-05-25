#pragma once

#include "TaskSession.h"

#include <string>

namespace gisqc {

class ReportExporter {
public:
    static std::string toHtmlReport(const TaskSessionReport& report);
    static std::string toExcelHtmlReport(const TaskSessionReport& report);
    static std::string toWordHtmlReport(const TaskSessionReport& report);
    static std::string defaultReportBaseName(const TaskSessionReport& report);
    static std::string escapeHtml(const std::string& value);
    static const char* severityText(Severity severity);
};

} // namespace gisqc
