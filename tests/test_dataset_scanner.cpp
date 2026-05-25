#include "../src/core/DatasetScanner.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
static std::string pathToUtf8(const fs::path& path) {
    const auto wide = path.wstring();
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), size, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}
#else
static std::string pathToUtf8(const fs::path& path) {
    return path.u8string();
}
#endif

static void writeFile(const fs::path& path) {
    std::ofstream out(path);
    out << "sample";
}

static void testScannerRecognizesSupportedDatasets() {
    fs::path temp = fs::temp_directory_path() / "gis_qc_scanner_test";
    fs::remove_all(temp);
    fs::create_directories(temp / fs::u8path(u8"待检DB.gdb"));
    fs::create_directories(temp / fs::u8path(u8"二级目录"));
    writeFile(temp / fs::u8path(u8"GXDX_GD.shp"));
    writeFile(temp / fs::u8path(u8"二级目录") / "Nested.shp");
    writeFile(temp / fs::u8path(u8"成果包.gpkg"));
    writeFile(temp / fs::u8path(u8"说明.txt"));

    gisqc::DatasetScanner scanner;
    auto result = scanner.scan(pathToUtf8(temp));

    assert(result.rootPath == pathToUtf8(temp));
    assert(result.sources.size() == 4);
    assert(result.sources[0].type == gisqc::DatasetType::FileGDB);
    assert(result.sources[0].displayName == "待检DB.gdb");
    assert(result.sources[1].type == gisqc::DatasetType::Shapefile);
    assert(result.sources[1].layerName == "GXDX_GD");
    assert(result.sources[2].type == gisqc::DatasetType::Shapefile);
    assert(result.sources[2].layerName == "Nested");
    assert(result.sources[3].type == gisqc::DatasetType::GeoPackage);
    assert(result.sources[3].displayName == "成果包.gpkg");

    fs::remove_all(temp);
}

static void testScannerReportsMissingPath() {
    gisqc::DatasetScanner scanner;
    auto result = scanner.scan("/path/that/does/not/exist");

    assert(result.sources.empty());
    assert(!result.messages.empty());
    assert(result.messages.front().find("不存在") != std::string::npos);
}

int main() {
    testScannerRecognizesSupportedDatasets();
    testScannerReportsMissingPath();
    std::cout << "DatasetScanner tests passed\n";
    return 0;
}
