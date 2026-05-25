#include "ReportExporter.h"

#include <map>
#include <sstream>

namespace gisqc {

namespace {

struct IssueGroupStats {
    int total{0};
    int error{0};
    int warning{0};
};

void addSeverity(IssueGroupStats& stats, Severity severity) {
    ++stats.total;
    if (severity == Severity::Error) {
        ++stats.error;
    } else if (severity == Severity::Warning) {
        ++stats.warning;
    }
}

std::map<std::string, IssueGroupStats> groupByRule(const TaskSessionReport& report) {
    std::map<std::string, IssueGroupStats> grouped;
    for (const auto& issue : report.issues) {
        addSeverity(grouped[issue.ruleCode.empty() ? "未标识规则" : issue.ruleCode], issue.severity);
    }
    return grouped;
}

std::map<std::string, IssueGroupStats> groupByLayer(const TaskSessionReport& report) {
    std::map<std::string, IssueGroupStats> grouped;
    for (const auto& issue : report.issues) {
        addSeverity(grouped[issue.layerName.empty() ? "未标识对象" : issue.layerName], issue.severity);
    }
    return grouped;
}

std::string conclusionText(const TaskSessionReport& report) {
    if (report.statistics.errorCount > 0) {
        return "本次检查发现错误级问题，建议完成数据返修并复核后再对外提交。";
    }
    if (report.statistics.warningCount > 0) {
        return "本次检查未发现错误级问题，但存在警告项，建议结合项目规范确认是否需要修正。";
    }
    return "本次检查未发现错误或警告问题，可作为阶段性交付通过依据。";
}

void appendIssueRows(std::ostringstream& out, const TaskSessionReport& report) {
    for (const auto& issue : report.issues) {
        out << "<tr>"
            << "<td>" << ReportExporter::escapeHtml(issue.issueId) << "</td>"
            << "<td>" << ReportExporter::escapeHtml(issue.ruleCode) << "</td>"
            << "<td>" << ReportExporter::escapeHtml(issue.layerName) << "</td>"
            << "<td>" << ReportExporter::escapeHtml(issue.featureId) << "</td>"
            << "<td>" << ReportExporter::escapeHtml(issue.description) << "</td>"
            << "<td>" << ReportExporter::severityText(issue.severity) << "</td>"
            << "<td>" << ReportExporter::escapeHtml(issue.status) << "</td>"
            << "</tr>\n";
    }
    if (report.issues.empty()) {
        out << "<tr><td colspan=\"7\">未发现问题</td></tr>\n";
    }
}

void appendGroupedRows(std::ostringstream& out, const std::map<std::string, IssueGroupStats>& grouped) {
    if (grouped.empty()) {
        out << "<tr><td colspan=\"4\">无问题统计</td></tr>\n";
        return;
    }
    for (const auto& [name, stats] : grouped) {
        out << "<tr><td>" << ReportExporter::escapeHtml(name) << "</td>"
            << "<td>" << stats.total << "</td>"
            << "<td>" << stats.error << "</td>"
            << "<td>" << stats.warning << "</td></tr>\n";
    }
}

void appendSummaryTable(std::ostringstream& out, const TaskSessionReport& report) {
    out << "<table>"
        << "<tr><td>任务名称</td><td>" << ReportExporter::escapeHtml(report.task.name()) << "</td></tr>"
        << "<tr><td>成果目录</td><td>" << ReportExporter::escapeHtml(report.task.inputPath()) << "</td></tr>"
        << "<tr><td>数据源数量</td><td>" << report.scan.sourceCount << "</td></tr>"
        << "<tr><td>启用规则数</td><td>" << report.statistics.totalRules << "</td></tr>"
        << "<tr><td>通过规则数</td><td>" << report.statistics.passedRules << "</td></tr>"
        << "<tr><td>警告数</td><td>" << report.statistics.warningCount << "</td></tr>"
        << "<tr><td>错误数</td><td>" << report.statistics.errorCount << "</td></tr>"
        << "<tr><td>通过率</td><td>" << ReportExporter::escapeHtml(report.statistics.passRateText) << "</td></tr>"
        << "</table>";
}

void appendGroupedTable(std::ostringstream& out, const std::string& title, const std::map<std::string, IssueGroupStats>& grouped) {
    out << "<h3>" << ReportExporter::escapeHtml(title) << "</h3>"
        << "<table><tr><th>分类</th><th>问题数</th><th>错误</th><th>警告</th></tr>";
    appendGroupedRows(out, grouped);
    out << "</table>";
}

} // namespace

std::string ReportExporter::toHtmlReport(const TaskSessionReport& report) {
    std::ostringstream out;
    out << "<!doctype html><html><head><meta charset=\"utf-8\">"
        << "<title>GIS 数据质检报告</title>"
        << "<style>"
        << "body{font-family:'Microsoft YaHei',Arial,sans-serif;background:#f3f7fc;color:#25364d;margin:32px;}"
        << "h1{color:#10233f;} h2{margin-top:28px;color:#10233f}.meta,.card{background:#fff;border:1px solid #d8e4f2;border-radius:10px;padding:18px;margin:16px 0;}"
        << ".stats{display:grid;grid-template-columns:repeat(5,1fr);gap:14px}.stat{background:#fff;border:1px solid #d8e4f2;border-radius:10px;padding:16px;}"
        << ".value{font-size:30px;font-weight:800;color:#2778d7}.green{color:#16a34a}.orange{color:#f59e0b}.red{color:#dc2626}"
        << "table{width:100%;border-collapse:collapse;background:#fff;border:1px solid #d8e4f2;border-radius:10px;overflow:hidden;margin:10px 0 18px;}"
        << "th{background:#edf5ff;color:#123456;text-align:left;padding:10px;border-bottom:1px solid #d8e4f2;}td{padding:9px;border-bottom:1px solid #edf2f8;}"
        << "</style></head><body>";
    out << "<h1>GIS 数据质检报告</h1>";
    out << "<div class=\"meta\"><b>任务：</b>" << escapeHtml(report.task.name())
        << "<br><b>成果目录：</b>" << escapeHtml(report.task.inputPath())
        << "<br><b>数据源数量：</b>" << report.scan.sourceCount << "</div>";
    out << "<h2>统计摘要</h2><div class=\"stats\">"
        << "<div class=\"stat\">总规则数<div class=\"value\">" << report.statistics.totalRules << "</div></div>"
        << "<div class=\"stat\">已通过<div class=\"value green\">" << report.statistics.passedRules << "</div></div>"
        << "<div class=\"stat\">警告<div class=\"value orange\">" << report.statistics.warningCount << "</div></div>"
        << "<div class=\"stat\">错误<div class=\"value red\">" << report.statistics.errorCount << "</div></div>"
        << "<div class=\"stat\">通过率<div class=\"value\">" << escapeHtml(report.statistics.passRateText) << "</div></div>"
        << "</div>";
    out << "<div class=\"card\"><h2>规则清单</h2><p>本次启用规则共 " << report.statistics.totalRules
        << " 条；详细规则名称、参数和支持状态请参见交付包内《支持规则清单.md》。</p></div>";
    out << "<div class=\"card\"><h2>分组统计</h2>";
    appendGroupedTable(out, "按规则统计", groupByRule(report));
    appendGroupedTable(out, "按图层统计", groupByLayer(report));
    out << "</div>";
    out << "<div class=\"card\"><h2>问题清单</h2><table><thead><tr>"
        << "<th>问题编号</th><th>规则编码</th><th>图层/对象</th><th>要素/文件</th><th>问题描述</th><th>严重级别</th><th>状态</th>"
        << "</tr></thead><tbody>";
    appendIssueRows(out, report);
    out << "</tbody></table></div>";
    out << "<div class=\"card\"><h2>检查结论</h2><p>" << escapeHtml(conclusionText(report)) << "</p></div>";
    out << "</body></html>";
    return out.str();
}

std::string ReportExporter::toExcelHtmlReport(const TaskSessionReport& report) {
    std::ostringstream out;
    out << "<html xmlns:o=\"urn:schemas-microsoft-com:office:office\" "
        << "xmlns:x=\"urn:schemas-microsoft-com:office:excel\" "
        << "xmlns=\"http://www.w3.org/TR/REC-html40\"><head><meta charset=\"utf-8\"></head><body>";
    out << "<h2>GIS 数据质检报告</h2>";
    out << "<h3>统计</h3>";
    appendSummaryTable(out, report);
    appendGroupedTable(out, "按规则统计", groupByRule(report));
    appendGroupedTable(out, "按图层统计", groupByLayer(report));
    out << "<h3>问题清单</h3><table border=\"1\"><tr>"
        << "<th>问题编号</th><th>规则编码</th><th>图层/对象</th><th>要素/文件</th><th>问题描述</th><th>严重级别</th><th>状态</th></tr>";
    appendIssueRows(out, report);
    out << "</table></body></html>";
    return out.str();
}

std::string ReportExporter::toWordHtmlReport(const TaskSessionReport& report) {
    std::ostringstream out;
    out << "<html xmlns:o=\"urn:schemas-microsoft-com:office:office\" "
        << "xmlns:w=\"urn:schemas-microsoft-com:office:word\" "
        << "xmlns=\"http://www.w3.org/TR/REC-html40\"><head><meta charset=\"utf-8\">"
        << "<title>GIS 数据质检报告</title>"
        << "<style>"
        << "body{font-family:'Microsoft YaHei',Arial,sans-serif;color:#25364d;line-height:1.55;}"
        << "h1{color:#10233f;border-bottom:2px solid #2778d7;padding-bottom:8px;}"
        << "h2{color:#10233f;margin-top:24px;}"
        << "table{width:100%;border-collapse:collapse;margin:12px 0;}th,td{border:1px solid #b8cbe0;padding:7px;text-align:left;}"
        << "th{background:#edf5ff;color:#123456;}.error{color:#dc2626;font-weight:bold}.warning{color:#f59e0b;font-weight:bold}"
        << ".cover{text-align:center;margin:80px 0 120px}.cover h1{font-size:34px;border-bottom:0}.muted{color:#667085}"
        << "</style></head><body>";
    out << "<div class=\"cover\"><h1>GIS 数据质检报告</h1><p>封面</p><p class=\"muted\">任务名称："
        << escapeHtml(report.task.name()) << "</p><p class=\"muted\">报告类型：自动质检报告</p></div>";
    out << "<h2>一、项目基本信息</h2>";
    appendSummaryTable(out, report);
    out << "<h2>二、检查范围</h2><p>本次检查对象为：" << escapeHtml(report.task.inputPath())
        << "。软件已识别数据源 " << report.scan.sourceCount << " 个。</p>";
    out << "<h2>三、规则清单</h2><p>本次启用规则共 " << report.statistics.totalRules
        << " 条。完整规则编码、说明、示例和参数解释请参见交付包内《支持规则清单.md》。</p>";
    out << "<h2>四、统计摘要</h2>";
    appendSummaryTable(out, report);
    appendGroupedTable(out, "按规则统计", groupByRule(report));
    appendGroupedTable(out, "按图层统计", groupByLayer(report));
    out << "<h2>五、问题明细</h2><table><tr>"
        << "<th>问题编号</th><th>规则编码</th><th>图层/对象</th><th>要素/文件</th><th>问题描述</th><th>严重级别</th><th>状态</th></tr>";
    appendIssueRows(out, report);
    out << "</table><h2>六、检查结论</h2><p>" << escapeHtml(conclusionText(report)) << "</p>"
        << "<h2>七、处理建议</h2>"
        << "<p>建议先处理错误级问题，再处理警告项。导出的问题清单可作为数据返修和复核依据；完成返修后应重新运行质检并保存复核报告。</p>"
        << "</body></html>";
    return out.str();
}

std::string ReportExporter::defaultReportBaseName(const TaskSessionReport& report) {
    return report.task.name().empty() ? "GIS数据质检报告" : report.task.name();
}

std::string ReportExporter::escapeHtml(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

const char* ReportExporter::severityText(Severity severity) {
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
