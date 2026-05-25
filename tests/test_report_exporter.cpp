#include "../src/core/ReportExporter.h"

#include <cassert>
#include <iostream>

static gisqc::TaskSessionReport makeReport() {
    gisqc::TaskSessionReport report{gisqc::TaskModel::create("交付报告测试", "D:/sample")};
    report.scan.sourceCount = 2;
    report.statistics.totalRules = 10;
    report.statistics.passedRules = 8;
    report.statistics.warningCount = 1;
    report.statistics.errorCount = 1;
    report.statistics.passRateText = "80.0%";

    gisqc::IssueRecord errorIssue;
    errorIssue.issueId = "P-0001";
    errorIssue.ruleCode = "C030303";
    errorIssue.layerName = "GXDT_GD";
    errorIssue.featureId = "42";
    errorIssue.description = "面几何无效";
    errorIssue.severity = gisqc::Severity::Error;
    errorIssue.status = "待处理";
    report.issues.push_back(errorIssue);

    gisqc::IssueRecord warningIssue;
    warningIssue.issueId = "P-0002";
    warningIssue.ruleCode = "A010302";
    warningIssue.layerName = "成果文件";
    warningIssue.featureId = "bad name.gpkg";
    warningIssue.description = "文件命名不符合规范";
    warningIssue.severity = gisqc::Severity::Warning;
    warningIssue.status = "待处理";
    report.issues.push_back(warningIssue);
    return report;
}

int main() {
    const auto report = makeReport();
    const auto html = gisqc::ReportExporter::toHtmlReport(report);
    const auto excel = gisqc::ReportExporter::toExcelHtmlReport(report);
    const auto word = gisqc::ReportExporter::toWordHtmlReport(report);

    assert(html.find("GIS 数据质检报告") != std::string::npos);
    assert(html.find("C030303") != std::string::npos);
    assert(html.find("统计摘要") != std::string::npos);
    assert(html.find("按规则统计") != std::string::npos);
    assert(html.find("按图层统计") != std::string::npos);
    assert(html.find("检查结论") != std::string::npos);

    assert(excel.find("urn:schemas-microsoft-com:office:excel") != std::string::npos);
    assert(excel.find("交付报告测试") != std::string::npos);
    assert(excel.find("统计") != std::string::npos);
    assert(excel.find("按规则统计") != std::string::npos);
    assert(excel.find("按图层统计") != std::string::npos);

    assert(word.find("urn:schemas-microsoft-com:office:word") != std::string::npos);
    assert(word.find("封面") != std::string::npos);
    assert(word.find("项目基本信息") != std::string::npos);
    assert(word.find("检查范围") != std::string::npos);
    assert(word.find("规则清单") != std::string::npos);
    assert(word.find("统计摘要") != std::string::npos);
    assert(word.find("问题明细") != std::string::npos);
    assert(word.find("检查结论") != std::string::npos);
    assert(word.find("处理建议") != std::string::npos);

    std::cout << "ReportExporter tests passed\n";
    return 0;
}
