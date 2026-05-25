#include "core/IssueCsvExporter.h"
#include "core/IssueRecord.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace gisqc;

int main() {
    std::vector<IssueRecord> issues;
    issues.push_back({"P-0001", "A010103", "成果目录", "文档资料", "缺少必交付子目录：文档资料", Severity::Error, "待处理"});
    issues.push_back({"P-0002", "A010302", "成果目录", "bad,name.shp", "文件命名不符合规范：bad,name.shp", Severity::Warning, "待处理"});

    const auto csv = IssueCsvExporter::toCsv(issues);
    assert(csv.find("问题编号,规则编码,图层/对象,要素/文件,问题描述,严重级别,状态") == 0);
    assert(csv.find("P-0001,A010103") != std::string::npos);
    assert(csv.find("错误") != std::string::npos);
    assert(csv.find("警告") != std::string::npos);
    assert(csv.find("\"bad,name.shp\"") != std::string::npos);

    std::cout << "IssueCsvExporter tests passed\n";
    return 0;
}
