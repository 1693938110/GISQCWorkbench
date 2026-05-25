#pragma once

#include "IssueRecord.h"

#include <string>
#include <vector>

namespace gisqc {

class IssueCsvExporter {
public:
    static std::string toCsv(const std::vector<IssueRecord>& issues);

private:
    static std::string escape(const std::string& value);
    static const char* severityText(Severity severity);
};

} // namespace gisqc
