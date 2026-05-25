#pragma once

#include "IssueRecord.h"

#include <string>
#include <vector>

namespace gisqc {

class IssuePreviewExporter {
public:
    static std::string toHtml(const std::vector<IssueRecord>& issues, const std::string& title = "问题图斑预览");

private:
    static std::string escapeHtml(const std::string& value);
    static std::string escapeJs(const std::string& value);
};

} // namespace gisqc
