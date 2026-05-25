#include "../src/core/DatasetScanService.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

static void touch(const fs::path& path) {
    std::ofstream out(path);
    out << "sample";
}

static void testBuildsSummaryRowsForUiTable() {
    fs::path temp = fs::temp_directory_path() / "gis_qc_scan_service_test";
    fs::remove_all(temp);
    fs::create_directories(temp / fs::u8path(u8"待检DB.gdb"));
    touch(temp / fs::u8path(u8"GXDX_GD.shp"));
    touch(temp / fs::u8path(u8"GXDX_GD.shx"));
    {
        unsigned char header[32]{};
        header[0] = 0x03;
        header[4] = 7;
        std::ofstream out(temp / "GXDX_GD.dbf", std::ios::binary);
        out.write(reinterpret_cast<const char*>(header), sizeof(header));
    }
    {
        std::ofstream out(temp / "GXDX_GD.prj");
        out << "GEOGCS[\"CGCS2000\"]";
    }
    touch(temp / fs::u8path(u8"成果包.gpkg"));

    gisqc::DatasetScanService service;
    auto summary = service.scan(temp.u8string());

    assert(summary.sourceCount == 3);
    assert(summary.rows.size() == 3);

    const auto findRow = [&](const std::string& name) -> const gisqc::DatasetTableRow* {
        for (const auto& row : summary.rows) {
            if (row.name == name) {
                return &row;
            }
        }
        return nullptr;
    };

    const auto* gdbRow = findRow("待检DB.gdb");
    assert(gdbRow);
    assert(gdbRow->type == "FileGDB");
    assert(!gdbRow->featureCountText.empty());
    assert(!gdbRow->crsText.empty());
    assert(!gdbRow->status.empty());

    const auto* shpRow = findRow("GXDX_GD.shp");
    assert(shpRow);
    assert(shpRow->featureCountText == "7");
    assert(shpRow->crsText.find("CGCS2000") != std::string::npos);
    assert(shpRow->status == "元数据已读取");

    const auto* gpkgRow = findRow("成果包.gpkg");
    assert(gpkgRow);
    assert(gpkgRow->type == "GeoPackage");
    assert(!gpkgRow->status.empty());
    assert(!summary.logs.empty());

    fs::remove_all(temp);
}

static void testMissingPathReturnsErrorLog() {
    gisqc::DatasetScanService service;
    auto summary = service.scan("/missing/path");

    assert(summary.sourceCount == 0);
    assert(summary.rows.empty());
    assert(!summary.logs.empty());
    assert(summary.logs[0].find("不存在") != std::string::npos);
}

int main() {
    testBuildsSummaryRowsForUiTable();
    testMissingPathReturnsErrorLog();
    std::cout << "DatasetScanService tests passed\n";
    return 0;
}
