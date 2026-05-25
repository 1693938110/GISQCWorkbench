#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

static std::string readText(const std::string& path) {
    std::ifstream input(path);
    assert(input && "expected source file to exist");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

static void assertContains(const std::string& text, const std::string& needle) {
    assert(text.find(needle) != std::string::npos && "expected source integration marker missing");
}

int main() {
    const auto executionHeader = readText("src/ui/ExecutionPage.h");
    const auto executionSource = readText("src/ui/ExecutionPage.cpp");
    const auto mainHeader = readText("src/ui/MainWindow.h");
    const auto mainSource = readText("src/ui/MainWindow.cpp");
    const auto resultsSource = readText("src/ui/ResultsPage.cpp");
    const auto cmake = readText("CMakeLists.txt");

    assertContains(executionHeader, "reportReady");
    assertContains(executionHeader, "runQualityCheck");
    assertContains(executionSource, "RuleTemplateStore");
    assertContains(executionSource, "TaskSession");
    assertContains(executionSource, "activeTemplatePath");
    assertContains(executionSource, "QFileDialog::getExistingDirectory");
    assertContains(executionSource, "emit reportReady(report)");

    assertContains(mainHeader, "handleTaskReport");
    assertContains(mainSource, "ExecutionPage");
    assertContains(mainSource, "resultsPage_");
    assertContains(mainSource, "showReport(report)");
    assertContains(mainSource, "setCurrentWidget(resultsPage_)");

    assertContains(resultsSource, "IssueCsvExporter");
    assertContains(resultsSource, "exportIssueCsv");
    assertContains(resultsSource, "QFileDialog::getSaveFileName");
    assertContains(resultsSource, "currentReport_");

    assertContains(cmake, "src/ui/ExecutionPage.cpp");
    assertContains(cmake, "src/ui/ExecutionPage.h");

    return 0;
}
