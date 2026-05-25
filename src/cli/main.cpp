#include "../core/IssueCsvExporter.h"
#include "../core/IssuePreviewExporter.h"
#include "../core/LicenseManager.h"
#include "../core/RuleTemplateStore.h"
#include "../core/TaskSession.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef GISQC_HAVE_GDAL
#include <cpl_conv.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void printUsage() {
    std::cout << "GISQCWorkbenchCLI - GIS data quality check command line preview\n"
              << "Usage:\n"
              << "  GISQCWorkbenchCLI.exe <dataset-folder> [output-csv]\n\n"
              << "This is an interim Windows executable for validating the core engine.\n"
              << "The Qt desktop executable still requires Qt6 SDK on Windows.\n";
}

#ifdef _WIN32
std::string wideToUtf8(const wchar_t* value) {
    if (!value) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}
#endif

std::filesystem::path pathFromUtf8(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return std::filesystem::path(text);
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
    wide.resize(static_cast<std::size_t>(size - 1));
    return std::filesystem::path(wide);
#else
    return std::filesystem::u8path(text);
#endif
}

std::string pathToUtf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto wide = path.wstring();
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
#else
    return path.u8string();
#endif
}

std::filesystem::path executableDirectory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD size = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (size > 0 && size < MAX_PATH) {
        return std::filesystem::path(buffer).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

void configureRuntimeEnvironment() {
#ifdef GISQC_HAVE_GDAL
    const auto base = executableDirectory();
    const auto gdalData = base / "gdal" / "share" / "gdal";
    const auto projData = base / "proj";
    if (std::filesystem::exists(gdalData)) {
        CPLSetConfigOption("GDAL_DATA", pathToUtf8(gdalData).c_str());
    }
    if (std::filesystem::exists(projData)) {
        CPLSetConfigOption("PROJ_DATA", pathToUtf8(projData).c_str());
        CPLSetConfigOption("PROJ_LIB", pathToUtf8(projData).c_str());
    }
#endif
}

void writeUtf8File(const std::filesystem::path& path, const std::string& content) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("无法写入输出文件：" + pathToUtf8(path));
    }
    out << content;
    if (!out) {
        throw std::runtime_error("输出文件写入失败：" + pathToUtf8(path));
    }
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
#else
int main(int argc, char* argv[]) {
#endif
    if (argc < 2) {
        printUsage();
        std::cout << "Machine code: " << gisqc::LicenseManager::machineCode() << '\n'
                  << "License path: " << gisqc::LicenseManager::defaultLicensePath().u8string() << '\n';
        return 0;
    }

#ifdef _WIN32
    const std::string inputPath = wideToUtf8(argv[1]);
    const std::string outputCsv = argc >= 3 ? wideToUtf8(argv[2]) : "GISQCWorkbenchCLI_issues.csv";
#else
    const std::string inputPath = argv[1];
    const std::string outputCsv = argc >= 3 ? argv[2] : "GISQCWorkbenchCLI_issues.csv";
#endif

    try {
        configureRuntimeEnvironment();

        const auto license = gisqc::LicenseManager::check(gisqc::LicenseManager::defaultLicensePath(),
                                                          gisqc::LicenseManager::defaultStatePath(),
                                                          true);
        if (!license.valid) {
            std::cerr << "License failed: " << license.message << '\n'
                      << "Machine code: " << gisqc::LicenseManager::machineCode() << '\n'
                      << "Please use LicenseGenerator.exe to generate license.dat and place it at: "
                      << gisqc::LicenseManager::defaultLicensePath().u8string() << '\n';
            return 3;
        }

        const auto inputFsPath = pathFromUtf8(inputPath);
        if (!std::filesystem::exists(inputFsPath)) {
            std::cerr << "Failed: 输入目录不存在：" << inputPath << '\n';
            return 1;
        }
        if (!std::filesystem::is_directory(inputFsPath)) {
            std::cerr << "Failed: 输入路径不是目录：" << inputPath << '\n';
            return 1;
        }

        gisqc::RuleTemplateStore store;
        const auto templ = store.loadActive();

        gisqc::TaskSession session;
        const auto report = session.run("CLI质检任务", inputPath, templ.rules, templ.globalTolerance);

        const std::string bom = "\xEF\xBB\xBF";
        const auto csvPath = pathFromUtf8(outputCsv);
        writeUtf8File(csvPath, bom + gisqc::IssueCsvExporter::toCsv(report.issues));
        auto previewPath = csvPath;
        previewPath.replace_extension(".preview.html");
        writeUtf8File(previewPath, gisqc::IssuePreviewExporter::toHtml(report.issues, "CLI质检问题图斑预览"));

        std::cout << "Task: " << report.task.name() << '\n'
                  << "Input: " << report.task.inputPath() << '\n'
                  << "Data sources: " << report.scan.sourceCount << '\n'
                  << "Rules: " << report.statistics.totalRules << '\n'
                  << "Issues: " << report.issues.size() << '\n'
                  << "Pass rate: " << report.statistics.passRateText << '\n'
                  << "CSV: " << outputCsv << '\n'
                  << "Preview: " << pathToUtf8(previewPath) << '\n';
        return report.issues.empty() ? 0 : 2;
    } catch (const std::exception& ex) {
        std::cerr << "Failed: " << ex.what() << '\n';
        return 1;
    }
}
