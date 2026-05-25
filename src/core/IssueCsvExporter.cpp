#include "IssueCsvExporter.h"

#include <iomanip>
#include <sstream>

namespace gisqc {

std::string IssueCsvExporter::toCsv(const std::vector<IssueRecord>& issues) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(8);
    out << "问题编号,规则编码,图层/对象,要素/文件,问题描述,严重级别,状态,数据源路径,预览X,预览Y,几何WKT\n";
    for (const auto& issue : issues) {
        out << escape(issue.issueId) << ','
            << escape(issue.ruleCode) << ','
            << escape(issue.layerName) << ','
            << escape(issue.featureId) << ','
            << escape(issue.description) << ','
            << severityText(issue.severity) << ','
            << escape(issue.status) << ','
            << escape(issue.sourcePath) << ','
            << (issue.hasPreviewGeometry ? std::to_string(issue.previewX) : "") << ','
            << (issue.hasPreviewGeometry ? std::to_string(issue.previewY) : "") << ','
            << escape(issue.geometryWkt) << '\n';
    }
    return out.str();
}

std::string IssueCsvExporter::escape(const std::string& value) {
    const bool needsQuotes = value.find_first_of(",\n\r\"") != std::string::npos;
    if (!needsQuotes) {
        return value;
    }
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

const char* IssueCsvExporter::severityText(Severity severity) {
    switch (severity) {
    case Severity::Warning:
        return "警告";
    case Severity::Error:
        return "错误";
    default:
        return "提示";
    }
}

} // namespace gisqc
