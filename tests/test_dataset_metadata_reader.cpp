#include "core/DatasetMetadataReader.h"
#include "core/DatasetScanner.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifdef GISQC_HAVE_GDAL
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#endif

using namespace gisqc;
namespace fs = std::filesystem;

static void writeDbfHeader(const fs::path& path, std::uint32_t recordCount) {
    unsigned char header[32]{};
    header[0] = 0x03;
    header[1] = 0x7c;
    header[2] = 0x01;
    header[3] = 0x01;
    header[4] = static_cast<unsigned char>(recordCount & 0xff);
    header[5] = static_cast<unsigned char>((recordCount >> 8) & 0xff);
    header[6] = static_cast<unsigned char>((recordCount >> 16) & 0xff);
    header[7] = static_cast<unsigned char>((recordCount >> 24) & 0xff);
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
}

static void writeText(const fs::path& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

#ifdef GISQC_HAVE_GDAL
static void createGeoPackage(const fs::path& path) {
    GDALAllRegister();
    fs::remove(path);
    fs::remove(fs::u8path(path.u8string() + "-journal"));
    fs::remove(fs::u8path(path.u8string() + "-wal"));
    fs::remove(fs::u8path(path.u8string() + "-shm"));
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    assert(driver && "GPKG driver should be available when GDAL is enabled");
    GDALDataset* dataset = driver->Create(path.u8string().c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    assert(dataset);

    OGRSpatialReference srs;
    srs.importFromEPSG(4490);
    OGRLayer* layer = dataset->CreateLayer("GXDX_GD", &srs, wkbPoint, nullptr);
    assert(layer);
    OGRFieldDefn nameField("name", OFTString);
    assert(layer->CreateField(&nameField) == OGRERR_NONE);

    for (int i = 0; i < 3; ++i) {
        OGRFeature* feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
        feature->SetField("name", ("P" + std::to_string(i + 1)).c_str());
        OGRPoint point(120.0 + i, 30.0 + i);
        feature->SetGeometry(&point);
        assert(layer->CreateFeature(feature) == OGRERR_NONE);
        OGRFeature::DestroyFeature(feature);
    }
    GDALClose(dataset);
}
#endif

int main() {
    const fs::path root = fs::temp_directory_path() / "gis_qc_dataset_metadata_reader_test";
    fs::remove_all(root);
    fs::create_directories(root);
    writeText(root / "GXDX_GD.shp", "placeholder");
    writeText(root / "GXDX_GD.shx", "placeholder");
    writeDbfHeader(root / "GXDX_GD.dbf", 42);
    writeText(root / "GXDX_GD.prj", "GEOGCS[\"CGCS2000\",DATUM[\"China_2000\"]]");

    DatasetSource source;
    source.type = DatasetType::Shapefile;
    source.displayName = "GXDX_GD.shp";
    source.layerName = "GXDX_GD";
    source.path = (root / "GXDX_GD.shp").u8string();

    DatasetMetadataReader reader;
    const auto metadata = reader.read(source);
    assert(metadata.featureCountKnown);
    assert(metadata.featureCount == 42);
    assert(metadata.featureCountText == "42");
    assert(metadata.crsText.find("CGCS2000") != std::string::npos);
    assert(metadata.status == "元数据已读取");
    assert(metadata.messages.empty());

    fs::remove(root / "GXDX_GD.dbf");
    const auto missing = reader.read(source);
    assert(!missing.featureCountKnown);
    assert(missing.featureCountText == "缺少DBF");
    assert(missing.status.find("缺少配套文件") != std::string::npos);
    assert(!missing.messages.empty());

#ifdef GISQC_HAVE_GDAL
    const fs::path gpkgPath = root / fs::u8path(u8"成果包.gpkg");
    createGeoPackage(gpkgPath);
    DatasetSource gpkg;
    gpkg.type = DatasetType::GeoPackage;
    gpkg.displayName = "成果包.gpkg";
    gpkg.layerName = "成果包";
    gpkg.path = gpkgPath.u8string();
    const auto gpkgMetadata = reader.read(gpkg);
    assert(gpkgMetadata.featureCountKnown);
    assert(gpkgMetadata.featureCount == 3);
    assert(gpkgMetadata.featureCountText == "3");
    assert(gpkgMetadata.crsText.find("CGCS2000") != std::string::npos || gpkgMetadata.crsText.find("4490") != std::string::npos);
    assert(gpkgMetadata.status.find("GDAL") != std::string::npos);
#endif

    fs::remove_all(root);
    std::cout << "DatasetMetadataReader tests passed\n";
    return 0;
}
