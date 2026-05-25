
#include "src/core/IssueCsvExporter.h"
#include "src/core/ReportExporter.h"
#include "src/core/RuleTemplateStore.h"
#include "src/core/TaskSession.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
static void writeUtf8(const fs::path& p, const std::string& s) { fs::create_directories(p.parent_path()); std::ofstream out(p, std::ios::binary|std::ios::trunc); out << "\xEF\xBB\xBF" << s; }
int main(){
 try{
  gisqc::RuleTemplateStore store;
  const auto templ=store.loadActive();
  gisqc::TaskSession session;
  const auto input=std::string("D:/jiedan/136/gis-qc-workbench/qa_synthetic_data/") + reinterpret_cast<const char*>(u8"03_GPKG空间几何问题包");
  auto report=session.run(reinterpret_cast<const char*>(u8"QA报告导出测试"), input, templ.rules);
  fs::path outdir=fs::u8path(u8"D:/jiedan/136/gis-qc-workbench/qa_test_outputs_ascii/reports");
  writeUtf8(outdir / "qa_report.html", gisqc::ReportExporter::toHtmlReport(report));
  writeUtf8(outdir / "qa_report.xls", gisqc::ReportExporter::toExcelHtmlReport(report));
  writeUtf8(outdir / "qa_report.doc", gisqc::ReportExporter::toWordHtmlReport(report));
  writeUtf8(outdir / "qa_report.csv", gisqc::IssueCsvExporter::toCsv(report.issues));
  std::cout << "issues=" << report.issues.size() << " outdir=" << outdir.u8string() << "\n";
  return 0;
 }catch(const std::exception& e){ std::cerr << "ERR " << e.what() << "\n"; return 1; }
}
