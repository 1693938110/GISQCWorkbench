#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cpl_conv.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

static std::string pathText(const fs::path& path) {
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

static fs::path executableDirectory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD size = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (size > 0 && size < MAX_PATH) {
        return fs::path(buffer).parent_path();
    }
#endif
    return fs::current_path();
}

static void configureGdalRuntime() {
    const auto base = executableDirectory();
    const auto gdalData = base / "gdal" / "share" / "gdal";
    const auto projData = base / "proj";
    if (fs::exists(gdalData)) {
        CPLSetConfigOption("GDAL_DATA", pathText(gdalData).c_str());
    }
    if (fs::exists(projData)) {
        CPLSetConfigOption("PROJ_DATA", pathText(projData).c_str());
        CPLSetConfigOption("PROJ_LIB", pathText(projData).c_str());
    }
}

static void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

static OGRSpatialReference cgcs2000() {
    OGRSpatialReference srs;
    if (srs.importFromEPSG(4490) != OGRERR_NONE) {
        throw std::runtime_error("failed to import EPSG:4490; check bundled PROJ_DATA/proj.db");
    }
    return srs;
}

static void removeGpkg(const fs::path& path) {
    fs::remove(path);
    fs::remove(fs::u8path(path.u8string() + "-journal"));
    fs::remove(fs::u8path(path.u8string() + "-wal"));
    fs::remove(fs::u8path(path.u8string() + "-shm"));
}

static void createCleanPackage(const fs::path& root) {
    fs::remove_all(root);
    fs::create_directories(root / fs::u8path(u8"空间数据"));
    fs::create_directories(root / fs::u8path(u8"文档资料"));
    fs::create_directories(root / fs::u8path(u8"元数据"));
    writeText(root / "readme.txt", "synthetic clean package\n");
    writeText(root / "metadata.xml", "<metadata>synthetic</metadata>\n");
    writeText(root / fs::u8path(u8"空间数据") / "keep.txt", "x");
    writeText(root / fs::u8path(u8"文档资料") / "keep.txt", "x");
    writeText(root / fs::u8path(u8"元数据") / "keep.txt", "x");

    const fs::path gpkg = root / fs::u8path(u8"空间数据") / "valid_core.gpkg";
    removeGpkg(gpkg);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver) throw std::runtime_error("GPKG driver not found");
    GDALDataset* ds = driver->Create(gpkg.u8string().c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) throw std::runtime_error("failed to create clean gpkg");
    auto srs = cgcs2000();
    const std::vector<std::string> layers = {"GXDT_GX", "GXDT_GD", "GXDL_GX", "GXDL_GD", "PCFW"};
    for (const auto& name : layers) {
        OGRLayer* layer = ds->CreateLayer(name.c_str(), &srs, wkbPoint, nullptr);
        if (!layer) throw std::runtime_error("failed to create layer " + name);
        OGRFieldDefn idField("ID", OFTInteger);
        layer->CreateField(&idField);
        OGRFeature* f = OGRFeature::CreateFeature(layer->GetLayerDefn());
        f->SetField("ID", 1);
        OGRPoint p(120.0, 30.0);
        f->SetGeometry(&p);
        layer->CreateFeature(f);
        OGRFeature::DestroyFeature(f);
    }
    GDALClose(ds);
}

static void createProblemPackage(const fs::path& root) {
    fs::remove_all(root);
    fs::create_directories(root / fs::u8path(u8"空间数据"));
    fs::create_directories(root / fs::u8path(u8"空目录"));
    writeText(root / "readme.txt", "problem package\n");
    // deliberately missing 文档资料, 元数据, metadata.xml
    writeText(root / "bad name.shp", "not a real shp");
    writeText(root / "GXDX_GD.shp", "not a real shp");
    writeText(root / "GXDX_GD.shx", "not a real shx");
    // deliberately missing GXDX_GD.dbf and GXDX_GD.prj
    writeText(root / fs::u8path(u8"中文 文件.shp"), "bad name and missing sidecars");
}

static void createGeometryPackage(const fs::path& root) {
    fs::remove_all(root);
    fs::create_directories(root / fs::u8path(u8"空间数据"));
    fs::create_directories(root / fs::u8path(u8"文档资料"));
    fs::create_directories(root / fs::u8path(u8"元数据"));
    writeText(root / "readme.txt", "geometry problem package\n");
    writeText(root / "metadata.xml", "<metadata>geometry</metadata>\n");
    writeText(root / fs::u8path(u8"文档资料") / "keep.txt", "x");
    writeText(root / fs::u8path(u8"元数据") / "keep.txt", "x");

    const fs::path gpkg = root / fs::u8path(u8"空间数据") / "geometry_checks.gpkg";
    removeGpkg(gpkg);
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver) throw std::runtime_error("GPKG driver not found");
    GDALDataset* ds = driver->Create(gpkg.u8string().c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds) throw std::runtime_error("failed to create geometry gpkg");
    auto srs = cgcs2000();

    OGRLayer* lineLayer = ds->CreateLayer("TinyLine", &srs, wkbLineString, nullptr);
    OGRFeature* lineFeature = OGRFeature::CreateFeature(lineLayer->GetLayerDefn());
    OGRLineString tinyLine;
    tinyLine.addPoint(120.0, 30.0);
    tinyLine.addPoint(120.000001, 30.0);
    lineFeature->SetGeometry(&tinyLine);
    lineLayer->CreateFeature(lineFeature);
    OGRFeature::DestroyFeature(lineFeature);

    OGRLayer* zLayer = ds->CreateLayer("ZPOINT", &srs, wkbPoint25D, nullptr);
    OGRFeature* zFeature = OGRFeature::CreateFeature(zLayer->GetLayerDefn());
    OGRPoint point(120.0, 30.0, 5.0);
    zFeature->SetGeometry(&point);
    zLayer->CreateFeature(zFeature);
    OGRFeature::DestroyFeature(zFeature);

    OGRLayer* outOfRangeLayer = ds->CreateLayer("OutOfRange", &srs, wkbPoint, nullptr);
    OGRFeature* outOfRangeFeature = OGRFeature::CreateFeature(outOfRangeLayer->GetLayerDefn());
    OGRPoint outOfRangePoint(125.0, 35.0);
    outOfRangeFeature->SetGeometry(&outOfRangePoint);
    outOfRangeLayer->CreateFeature(outOfRangeFeature);
    OGRFeature::DestroyFeature(outOfRangeFeature);

    OGRLayer* polyLayer = ds->CreateLayer("PolyChecks", &srs, wkbPolygon, nullptr);
    OGRFeature* sharpFeature = OGRFeature::CreateFeature(polyLayer->GetLayerDefn());
    OGRPolygon sharpPolygon;
    OGRLinearRing sharpRing;
    sharpRing.addPoint(0.0, 0.0);
    sharpRing.addPoint(1.0, 0.0);
    sharpRing.addPoint(0.99, 0.001);
    sharpRing.addPoint(0.0, 1.0);
    sharpRing.addPoint(0.0, 0.0);
    sharpPolygon.addRing(&sharpRing);
    sharpFeature->SetGeometry(&sharpPolygon);
    polyLayer->CreateFeature(sharpFeature);
    OGRFeature::DestroyFeature(sharpFeature);

    OGRFeature* holeFeature = OGRFeature::CreateFeature(polyLayer->GetLayerDefn());
    OGRPolygon holePolygon;
    OGRLinearRing outer;
    outer.addPoint(10.0, 10.0); outer.addPoint(11.0, 10.0); outer.addPoint(11.0, 11.0); outer.addPoint(10.0, 11.0); outer.addPoint(10.0, 10.0);
    OGRLinearRing inner;
    inner.addPoint(10.2, 10.2); inner.addPoint(10.4, 10.2); inner.addPoint(10.4, 10.4); inner.addPoint(10.2, 10.4); inner.addPoint(10.2, 10.2);
    holePolygon.addRing(&outer);
    holePolygon.addRing(&inner);
    holeFeature->SetGeometry(&holePolygon);
    polyLayer->CreateFeature(holeFeature);
    OGRFeature::DestroyFeature(holeFeature);

    OGRLayer* overlapLineLayer = ds->CreateLayer("OverlapLines", &srs, wkbLineString, nullptr);
    OGRFeature* overlapA = OGRFeature::CreateFeature(overlapLineLayer->GetLayerDefn());
    OGRLineString la; la.addPoint(0.0, 20.0); la.addPoint(2.0, 20.0); overlapA->SetGeometry(&la); overlapLineLayer->CreateFeature(overlapA); OGRFeature::DestroyFeature(overlapA);
    OGRFeature* overlapB = OGRFeature::CreateFeature(overlapLineLayer->GetLayerDefn());
    OGRLineString lb; lb.addPoint(1.0, 20.0); lb.addPoint(3.0, 20.0); overlapB->SetGeometry(&lb); overlapLineLayer->CreateFeature(overlapB); OGRFeature::DestroyFeature(overlapB);

    OGRLayer* crossingLineLayer = ds->CreateLayer("CrossingLines", &srs, wkbLineString, nullptr);
    OGRFeature* crossA = OGRFeature::CreateFeature(crossingLineLayer->GetLayerDefn());
    OGRLineString ca; ca.addPoint(0.0, 30.0); ca.addPoint(2.0, 32.0); crossA->SetGeometry(&ca); crossingLineLayer->CreateFeature(crossA); OGRFeature::DestroyFeature(crossA);
    OGRFeature* crossB = OGRFeature::CreateFeature(crossingLineLayer->GetLayerDefn());
    OGRLineString cb; cb.addPoint(0.0, 32.0); cb.addPoint(2.0, 30.0); crossB->SetGeometry(&cb); crossingLineLayer->CreateFeature(crossB); OGRFeature::DestroyFeature(crossB);

    OGRLayer* danglingLineLayer = ds->CreateLayer("DanglingLines", &srs, wkbLineString, nullptr);
    OGRFeature* danglingFeature = OGRFeature::CreateFeature(danglingLineLayer->GetLayerDefn());
    OGRLineString danglingLine; danglingLine.addPoint(0.0, 50.0); danglingLine.addPoint(1.0, 50.0);
    danglingFeature->SetGeometry(&danglingLine); danglingLineLayer->CreateFeature(danglingFeature); OGRFeature::DestroyFeature(danglingFeature);

    OGRLayer* pseudoNodeLayer = ds->CreateLayer("PseudoNodeLines", &srs, wkbLineString, nullptr);
    OGRFeature* pseudoNodeFeature = OGRFeature::CreateFeature(pseudoNodeLayer->GetLayerDefn());
    OGRLineString pseudoNodeLine; pseudoNodeLine.addPoint(0.0, 60.0); pseudoNodeLine.addPoint(1.0, 60.0); pseudoNodeLine.addPoint(2.0, 60.0);
    pseudoNodeFeature->SetGeometry(&pseudoNodeLine); pseudoNodeLayer->CreateFeature(pseudoNodeFeature);    OGRFeature::DestroyFeature(pseudoNodeFeature);

    OGRLayer* connectivityPseudoNodeLayer = ds->CreateLayer("ConnectivityPseudoNodeLines", &srs, wkbLineString, nullptr);
    OGRFeature* connectivityA = OGRFeature::CreateFeature(connectivityPseudoNodeLayer->GetLayerDefn());
    OGRLineString connectivityLineA; connectivityLineA.addPoint(0.0, 70.0); connectivityLineA.addPoint(1.0, 70.0);
    connectivityA->SetGeometry(&connectivityLineA); connectivityPseudoNodeLayer->CreateFeature(connectivityA); OGRFeature::DestroyFeature(connectivityA);
    OGRFeature* connectivityB = OGRFeature::CreateFeature(connectivityPseudoNodeLayer->GetLayerDefn());
    OGRLineString connectivityLineB; connectivityLineB.addPoint(1.0, 70.0); connectivityLineB.addPoint(2.0, 70.0);
    connectivityB->SetGeometry(&connectivityLineB); connectivityPseudoNodeLayer->CreateFeature(connectivityB); OGRFeature::DestroyFeature(connectivityB);

    OGRLayer* overlapPolyLayer = ds->CreateLayer("OverlapPolys", &srs, wkbPolygon, nullptr);
    OGRFeature* polyA = OGRFeature::CreateFeature(overlapPolyLayer->GetLayerDefn());
    OGRPolygon overlapPolyA;
    OGRLinearRing overlapOuterA;
    overlapOuterA.addPoint(20.0, 20.0); overlapOuterA.addPoint(22.0, 20.0); overlapOuterA.addPoint(22.0, 22.0); overlapOuterA.addPoint(20.0, 22.0); overlapOuterA.addPoint(20.0, 20.0);
    overlapPolyA.addRing(&overlapOuterA);
    polyA->SetGeometry(&overlapPolyA); overlapPolyLayer->CreateFeature(polyA); OGRFeature::DestroyFeature(polyA);
    OGRFeature* polyB = OGRFeature::CreateFeature(overlapPolyLayer->GetLayerDefn());
    OGRPolygon overlapPolyB;
    OGRLinearRing overlapOuterB;
    overlapOuterB.addPoint(21.0, 21.0); overlapOuterB.addPoint(23.0, 21.0); overlapOuterB.addPoint(23.0, 23.0); overlapOuterB.addPoint(21.0, 23.0); overlapOuterB.addPoint(21.0, 21.0);
    overlapPolyB.addRing(&overlapOuterB);
    polyB->SetGeometry(&overlapPolyB); overlapPolyLayer->CreateFeature(polyB);    OGRFeature::DestroyFeature(polyB);

    OGRLayer* gapPolyLayer = ds->CreateLayer("GapPolys", &srs, wkbPolygon, nullptr);
    OGRFeature* gapA = OGRFeature::CreateFeature(gapPolyLayer->GetLayerDefn());
    OGRPolygon gapPolyA;
    OGRLinearRing gapRingA;
    gapRingA.addPoint(30.0, 30.0); gapRingA.addPoint(31.0, 30.0); gapRingA.addPoint(31.0, 31.0); gapRingA.addPoint(30.0, 31.0); gapRingA.addPoint(30.0, 30.0);
    gapPolyA.addRing(&gapRingA);
    gapA->SetGeometry(&gapPolyA); gapPolyLayer->CreateFeature(gapA); OGRFeature::DestroyFeature(gapA);
    OGRFeature* gapB = OGRFeature::CreateFeature(gapPolyLayer->GetLayerDefn());
    OGRPolygon gapPolyB;
    OGRLinearRing gapRingB;
    gapRingB.addPoint(31.2, 30.0); gapRingB.addPoint(32.2, 30.0); gapRingB.addPoint(32.2, 31.0); gapRingB.addPoint(31.2, 31.0); gapRingB.addPoint(31.2, 30.0);
    gapPolyB.addRing(&gapRingB);
    gapB->SetGeometry(&gapPolyB); gapPolyLayer->CreateFeature(gapB); OGRFeature::DestroyFeature(gapB);

    OGRLayer* pseudoNodePolyLayer = ds->CreateLayer("PseudoNodePolys", &srs, wkbPolygon, nullptr);
    OGRFeature* pseudoPolyFeature = OGRFeature::CreateFeature(pseudoNodePolyLayer->GetLayerDefn());
    OGRPolygon pseudoPolygon;
    OGRLinearRing pseudoRing;
    pseudoRing.addPoint(40.0, 40.0);
    pseudoRing.addPoint(41.0, 40.0);
    pseudoRing.addPoint(42.0, 40.0);
    pseudoRing.addPoint(42.0, 41.0);
    pseudoRing.addPoint(40.0, 41.0);
    pseudoRing.addPoint(40.0, 40.0);
    pseudoPolygon.addRing(&pseudoRing);
    pseudoPolyFeature->SetGeometry(&pseudoPolygon);
    pseudoNodePolyLayer->CreateFeature(pseudoPolyFeature);
    OGRFeature::DestroyFeature(pseudoPolyFeature);

    OGRLayer* coincideRefPointLayer = ds->CreateLayer("CoincideReferencePoints", &srs, wkbPoint, nullptr);
    OGRPoint coincideRefPoint(70.0, 80.0);
    OGRFeature* coincideRefFeature = OGRFeature::CreateFeature(coincideRefPointLayer->GetLayerDefn());
    coincideRefFeature->SetGeometry(&coincideRefPoint); coincideRefPointLayer->CreateFeature(coincideRefFeature); OGRFeature::DestroyFeature(coincideRefFeature);
    OGRLayer* coincideSrcPointLayer = ds->CreateLayer("CoincideSourcePoints", &srs, wkbPoint, nullptr);
    OGRPoint coincideGoodPoint(70.0, 80.0);
    OGRFeature* coincideGoodFeature = OGRFeature::CreateFeature(coincideSrcPointLayer->GetLayerDefn());
    coincideGoodFeature->SetGeometry(&coincideGoodPoint); coincideSrcPointLayer->CreateFeature(coincideGoodFeature); OGRFeature::DestroyFeature(coincideGoodFeature);
    OGRPoint coincideBadPoint(70.5, 80.0);
    OGRFeature* coincideBadFeature = OGRFeature::CreateFeature(coincideSrcPointLayer->GetLayerDefn());
    coincideBadFeature->SetGeometry(&coincideBadPoint); coincideSrcPointLayer->CreateFeature(coincideBadFeature); OGRFeature::DestroyFeature(coincideBadFeature);

    OGRLayer* endpointRefLineLayer = ds->CreateLayer("PointEndpointReferenceLines", &srs, wkbLineString, nullptr);
    OGRLineString endpointRefLine; endpointRefLine.addPoint(72.0, 80.0); endpointRefLine.addPoint(73.0, 80.0);
    OGRFeature* endpointRefFeature = OGRFeature::CreateFeature(endpointRefLineLayer->GetLayerDefn());
    endpointRefFeature->SetGeometry(&endpointRefLine); endpointRefLineLayer->CreateFeature(endpointRefFeature); OGRFeature::DestroyFeature(endpointRefFeature);
    OGRLayer* endpointSrcPointLayer = ds->CreateLayer("PointEndpointSourcePoints", &srs, wkbPoint, nullptr);
    OGRPoint endpointGoodPoint(72.0, 80.0); OGRFeature* endpointGoodFeature = OGRFeature::CreateFeature(endpointSrcPointLayer->GetLayerDefn());
    endpointGoodFeature->SetGeometry(&endpointGoodPoint); endpointSrcPointLayer->CreateFeature(endpointGoodFeature); OGRFeature::DestroyFeature(endpointGoodFeature);
    OGRPoint endpointBadPoint(72.5, 80.0); OGRFeature* endpointBadFeature = OGRFeature::CreateFeature(endpointSrcPointLayer->GetLayerDefn());
    endpointBadFeature->SetGeometry(&endpointBadPoint); endpointSrcPointLayer->CreateFeature(endpointBadFeature); OGRFeature::DestroyFeature(endpointBadFeature);

    OGRLayer* pointLineRefLayer = ds->CreateLayer("PointLineReferenceLines", &srs, wkbLineString, nullptr);
    OGRLineString pointLineRef; pointLineRef.addPoint(74.0, 80.0); pointLineRef.addPoint(75.0, 80.0);
    OGRFeature* pointLineRefFeature = OGRFeature::CreateFeature(pointLineRefLayer->GetLayerDefn());
    pointLineRefFeature->SetGeometry(&pointLineRef); pointLineRefLayer->CreateFeature(pointLineRefFeature); OGRFeature::DestroyFeature(pointLineRefFeature);
    OGRLayer* pointLineSrcLayer = ds->CreateLayer("PointLineSourcePoints", &srs, wkbPoint, nullptr);
    OGRPoint pointLineGood(74.5, 80.0); OGRFeature* pointLineGoodFeature = OGRFeature::CreateFeature(pointLineSrcLayer->GetLayerDefn());
    pointLineGoodFeature->SetGeometry(&pointLineGood); pointLineSrcLayer->CreateFeature(pointLineGoodFeature); OGRFeature::DestroyFeature(pointLineGoodFeature);
    OGRPoint pointLineBad(74.5, 80.5); OGRFeature* pointLineBadFeature = OGRFeature::CreateFeature(pointLineSrcLayer->GetLayerDefn());
    pointLineBadFeature->SetGeometry(&pointLineBad); pointLineSrcLayer->CreateFeature(pointLineBadFeature); OGRFeature::DestroyFeature(pointLineBadFeature);

    OGRLayer* pointBoundaryPolyLayer = ds->CreateLayer("PointBoundaryPolys", &srs, wkbPolygon, nullptr);
    OGRPolygon pointBoundaryPoly; OGRLinearRing pointBoundaryRing; pointBoundaryRing.addPoint(76.0, 80.0); pointBoundaryRing.addPoint(77.0, 80.0); pointBoundaryRing.addPoint(77.0, 81.0); pointBoundaryRing.addPoint(76.0, 81.0); pointBoundaryRing.addPoint(76.0, 80.0); pointBoundaryPoly.addRing(&pointBoundaryRing);
    OGRFeature* pointBoundaryPolyFeature = OGRFeature::CreateFeature(pointBoundaryPolyLayer->GetLayerDefn());
    pointBoundaryPolyFeature->SetGeometry(&pointBoundaryPoly); pointBoundaryPolyLayer->CreateFeature(pointBoundaryPolyFeature); OGRFeature::DestroyFeature(pointBoundaryPolyFeature);
    OGRLayer* pointBoundarySrcLayer = ds->CreateLayer("PointBoundarySourcePoints", &srs, wkbPoint, nullptr);
    OGRPoint pointBoundaryGood(76.0, 80.5); OGRFeature* pointBoundaryGoodFeature = OGRFeature::CreateFeature(pointBoundarySrcLayer->GetLayerDefn());
    pointBoundaryGoodFeature->SetGeometry(&pointBoundaryGood); pointBoundarySrcLayer->CreateFeature(pointBoundaryGoodFeature); OGRFeature::DestroyFeature(pointBoundaryGoodFeature);
    OGRPoint pointBoundaryBad(76.5, 80.5); OGRFeature* pointBoundaryBadFeature = OGRFeature::CreateFeature(pointBoundarySrcLayer->GetLayerDefn());
    pointBoundaryBadFeature->SetGeometry(&pointBoundaryBad); pointBoundarySrcLayer->CreateFeature(pointBoundaryBadFeature); OGRFeature::DestroyFeature(pointBoundaryBadFeature);

    OGRLayer* pointInteriorPolyLayer = ds->CreateLayer("PointInteriorPolys", &srs, wkbPolygon, nullptr);
    OGRPolygon pointInteriorPoly; OGRLinearRing pointInteriorRing; pointInteriorRing.addPoint(78.0, 80.0); pointInteriorRing.addPoint(79.0, 80.0); pointInteriorRing.addPoint(79.0, 81.0); pointInteriorRing.addPoint(78.0, 81.0); pointInteriorRing.addPoint(78.0, 80.0); pointInteriorPoly.addRing(&pointInteriorRing);
    OGRFeature* pointInteriorPolyFeature = OGRFeature::CreateFeature(pointInteriorPolyLayer->GetLayerDefn());
    pointInteriorPolyFeature->SetGeometry(&pointInteriorPoly); pointInteriorPolyLayer->CreateFeature(pointInteriorPolyFeature); OGRFeature::DestroyFeature(pointInteriorPolyFeature);
    OGRLayer* pointInteriorSrcLayer = ds->CreateLayer("PointInteriorSourcePoints", &srs, wkbPoint, nullptr);
    OGRPoint pointInteriorGood(78.5, 80.5); OGRFeature* pointInteriorGoodFeature = OGRFeature::CreateFeature(pointInteriorSrcLayer->GetLayerDefn());
    pointInteriorGoodFeature->SetGeometry(&pointInteriorGood); pointInteriorSrcLayer->CreateFeature(pointInteriorGoodFeature); OGRFeature::DestroyFeature(pointInteriorGoodFeature);
    OGRPoint pointInteriorBad(78.0, 80.5); OGRFeature* pointInteriorBadFeature = OGRFeature::CreateFeature(pointInteriorSrcLayer->GetLayerDefn());
    pointInteriorBadFeature->SetGeometry(&pointInteriorBad); pointInteriorSrcLayer->CreateFeature(pointInteriorBadFeature); OGRFeature::DestroyFeature(pointInteriorBadFeature);

    OGRLayer* boundaryPolyLayer = ds->CreateLayer("BoundaryPolys", &srs, wkbPolygon, nullptr);
    OGRFeature* boundaryPolyFeature = OGRFeature::CreateFeature(boundaryPolyLayer->GetLayerDefn());
    OGRPolygon boundaryPolygon;
    OGRLinearRing boundaryRing;
    boundaryRing.addPoint(80.0, 80.0); boundaryRing.addPoint(82.0, 80.0); boundaryRing.addPoint(82.0, 82.0); boundaryRing.addPoint(80.0, 82.0); boundaryRing.addPoint(80.0, 80.0);
    boundaryPolygon.addRing(&boundaryRing);
    boundaryPolyFeature->SetGeometry(&boundaryPolygon);
    boundaryPolyLayer->CreateFeature(boundaryPolyFeature);
    OGRFeature::DestroyFeature(boundaryPolyFeature);

    OGRLayer* boundaryLineLayer = ds->CreateLayer("BoundaryLines", &srs, wkbLineString, nullptr);
    OGRFeature* boundaryLineFeature = OGRFeature::CreateFeature(boundaryLineLayer->GetLayerDefn());
    OGRLineString boundaryLine; boundaryLine.addPoint(80.0, 80.0); boundaryLine.addPoint(82.0, 80.0);
    boundaryLineFeature->SetGeometry(&boundaryLine); boundaryLineLayer->CreateFeature(boundaryLineFeature); OGRFeature::DestroyFeature(boundaryLineFeature);
    OGRFeature* offBoundaryLineFeature = OGRFeature::CreateFeature(boundaryLineLayer->GetLayerDefn());
    OGRLineString offBoundaryLine; offBoundaryLine.addPoint(80.0, 81.0); offBoundaryLine.addPoint(82.0, 81.0);
    offBoundaryLineFeature->SetGeometry(&offBoundaryLine); boundaryLineLayer->CreateFeature(offBoundaryLineFeature);    OGRFeature::DestroyFeature(offBoundaryLineFeature);

    OGRLayer* interiorPolyLayer = ds->CreateLayer("InteriorPolys", &srs, wkbPolygon, nullptr);
    OGRFeature* interiorPolyFeature = OGRFeature::CreateFeature(interiorPolyLayer->GetLayerDefn());
    OGRPolygon interiorPolygon;
    OGRLinearRing interiorRing;
    interiorRing.addPoint(90.0, 90.0); interiorRing.addPoint(92.0, 90.0); interiorRing.addPoint(92.0, 92.0); interiorRing.addPoint(90.0, 92.0); interiorRing.addPoint(90.0, 90.0);
    interiorPolygon.addRing(&interiorRing);
    interiorPolyFeature->SetGeometry(&interiorPolygon);
    interiorPolyLayer->CreateFeature(interiorPolyFeature);
    OGRFeature::DestroyFeature(interiorPolyFeature);

    OGRLayer* interiorLineLayer = ds->CreateLayer("InteriorLines", &srs, wkbLineString, nullptr);
    OGRFeature* interiorLineFeature = OGRFeature::CreateFeature(interiorLineLayer->GetLayerDefn());
    OGRLineString interiorLine; interiorLine.addPoint(90.2, 91.0); interiorLine.addPoint(91.8, 91.0);
    interiorLineFeature->SetGeometry(&interiorLine); interiorLineLayer->CreateFeature(interiorLineFeature); OGRFeature::DestroyFeature(interiorLineFeature);
    OGRFeature* boundaryInteriorLineFeature = OGRFeature::CreateFeature(interiorLineLayer->GetLayerDefn());
    OGRLineString boundaryInteriorLine; boundaryInteriorLine.addPoint(90.0, 90.0); boundaryInteriorLine.addPoint(92.0, 90.0);
    boundaryInteriorLineFeature->SetGeometry(&boundaryInteriorLine); interiorLineLayer->CreateFeature(boundaryInteriorLineFeature);    OGRFeature::DestroyFeature(boundaryInteriorLineFeature);

    OGRLayer* endpointPointLayer = ds->CreateLayer("EndpointPoints", &srs, wkbPoint, nullptr);
    OGRFeature* endpointA = OGRFeature::CreateFeature(endpointPointLayer->GetLayerDefn());
    OGRPoint endpointPointA(100.0, 100.0);
    endpointA->SetGeometry(&endpointPointA); endpointPointLayer->CreateFeature(endpointA); OGRFeature::DestroyFeature(endpointA);
    OGRFeature* endpointB = OGRFeature::CreateFeature(endpointPointLayer->GetLayerDefn());
    OGRPoint endpointPointB(101.0, 100.0);
    endpointB->SetGeometry(&endpointPointB); endpointPointLayer->CreateFeature(endpointB); OGRFeature::DestroyFeature(endpointB);

    OGRLayer* endpointLineLayer = ds->CreateLayer("EndpointLines", &srs, wkbLineString, nullptr);
    OGRFeature* coveredEndpointLineFeature = OGRFeature::CreateFeature(endpointLineLayer->GetLayerDefn());
    OGRLineString coveredEndpointLine; coveredEndpointLine.addPoint(100.0, 100.0); coveredEndpointLine.addPoint(101.0, 100.0);
    coveredEndpointLineFeature->SetGeometry(&coveredEndpointLine); endpointLineLayer->CreateFeature(coveredEndpointLineFeature); OGRFeature::DestroyFeature(coveredEndpointLineFeature);
    OGRFeature* missingEndpointLineFeature = OGRFeature::CreateFeature(endpointLineLayer->GetLayerDefn());
    OGRLineString missingEndpointLine; missingEndpointLine.addPoint(100.0, 100.0); missingEndpointLine.addPoint(102.0, 100.0);
    missingEndpointLineFeature->SetGeometry(&missingEndpointLine); endpointLineLayer->CreateFeature(missingEndpointLineFeature); OGRFeature::DestroyFeature(missingEndpointLineFeature);

    OGRLayer* coverReferenceLineLayer = ds->CreateLayer("CoverReferenceLines", &srs, wkbLineString, nullptr);
    OGRFeature* coverReferenceLineFeature = OGRFeature::CreateFeature(coverReferenceLineLayer->GetLayerDefn());
    OGRLineString coverReferenceLine; coverReferenceLine.addPoint(110.0, 110.0); coverReferenceLine.addPoint(112.0, 110.0);
    coverReferenceLineFeature->SetGeometry(&coverReferenceLine); coverReferenceLineLayer->CreateFeature(coverReferenceLineFeature); OGRFeature::DestroyFeature(coverReferenceLineFeature);
    OGRLayer* coverSourceLineLayer = ds->CreateLayer("CoverSourceLines", &srs, wkbLineString, nullptr);
    OGRFeature* coveredSourceLineFeature = OGRFeature::CreateFeature(coverSourceLineLayer->GetLayerDefn());
    OGRLineString coveredSourceLine; coveredSourceLine.addPoint(110.2, 110.0); coveredSourceLine.addPoint(111.8, 110.0);
    coveredSourceLineFeature->SetGeometry(&coveredSourceLine); coverSourceLineLayer->CreateFeature(coveredSourceLineFeature); OGRFeature::DestroyFeature(coveredSourceLineFeature);
    OGRFeature* uncoveredSourceLineFeature = OGRFeature::CreateFeature(coverSourceLineLayer->GetLayerDefn());
    OGRLineString uncoveredSourceLine; uncoveredSourceLine.addPoint(110.0, 110.2); uncoveredSourceLine.addPoint(112.0, 110.2);
    uncoveredSourceLineFeature->SetGeometry(&uncoveredSourceLine); coverSourceLineLayer->CreateFeature(uncoveredSourceLineFeature); OGRFeature::DestroyFeature(uncoveredSourceLineFeature);

    OGRLayer* noOverlapReferenceLineLayer = ds->CreateLayer("NoOverlapReferenceLines", &srs, wkbLineString, nullptr);
    OGRFeature* noOverlapReferenceLineFeature = OGRFeature::CreateFeature(noOverlapReferenceLineLayer->GetLayerDefn());
    OGRLineString noOverlapReferenceLine; noOverlapReferenceLine.addPoint(120.0, 120.0); noOverlapReferenceLine.addPoint(122.0, 120.0);
    noOverlapReferenceLineFeature->SetGeometry(&noOverlapReferenceLine); noOverlapReferenceLineLayer->CreateFeature(noOverlapReferenceLineFeature); OGRFeature::DestroyFeature(noOverlapReferenceLineFeature);
    OGRLayer* noOverlapSourceLineLayer = ds->CreateLayer("NoOverlapSourceLines", &srs, wkbLineString, nullptr);
    OGRFeature* disjointNoOverlapSourceFeature = OGRFeature::CreateFeature(noOverlapSourceLineLayer->GetLayerDefn());
    OGRLineString disjointNoOverlapSource; disjointNoOverlapSource.addPoint(120.0, 121.0); disjointNoOverlapSource.addPoint(122.0, 121.0);
    disjointNoOverlapSourceFeature->SetGeometry(&disjointNoOverlapSource); noOverlapSourceLineLayer->CreateFeature(disjointNoOverlapSourceFeature); OGRFeature::DestroyFeature(disjointNoOverlapSourceFeature);
    OGRFeature* overlappingNoOverlapSourceFeature = OGRFeature::CreateFeature(noOverlapSourceLineLayer->GetLayerDefn());
    OGRLineString overlappingNoOverlapSource; overlappingNoOverlapSource.addPoint(121.0, 120.0); overlappingNoOverlapSource.addPoint(123.0, 120.0);
    overlappingNoOverlapSourceFeature->SetGeometry(&overlappingNoOverlapSource); noOverlapSourceLineLayer->CreateFeature(overlappingNoOverlapSourceFeature); OGRFeature::DestroyFeature(overlappingNoOverlapSourceFeature);

    OGRLayer* noCrossReferenceLineLayer = ds->CreateLayer("NoCrossReferenceLines", &srs, wkbLineString, nullptr);
    OGRFeature* noCrossReferenceLineFeature = OGRFeature::CreateFeature(noCrossReferenceLineLayer->GetLayerDefn());
    OGRLineString noCrossReferenceLine; noCrossReferenceLine.addPoint(130.0, 130.0); noCrossReferenceLine.addPoint(132.0, 132.0);
    noCrossReferenceLineFeature->SetGeometry(&noCrossReferenceLine); noCrossReferenceLineLayer->CreateFeature(noCrossReferenceLineFeature); OGRFeature::DestroyFeature(noCrossReferenceLineFeature);
    OGRLayer* noCrossSourceLineLayer = ds->CreateLayer("NoCrossSourceLines", &srs, wkbLineString, nullptr);
    OGRFeature* sharedEndpointSourceFeature = OGRFeature::CreateFeature(noCrossSourceLineLayer->GetLayerDefn());
    OGRLineString sharedEndpointSource; sharedEndpointSource.addPoint(132.0, 132.0); sharedEndpointSource.addPoint(133.0, 132.0);
    sharedEndpointSourceFeature->SetGeometry(&sharedEndpointSource); noCrossSourceLineLayer->CreateFeature(sharedEndpointSourceFeature); OGRFeature::DestroyFeature(sharedEndpointSourceFeature);
    OGRFeature* crossingSourceFeature = OGRFeature::CreateFeature(noCrossSourceLineLayer->GetLayerDefn());
    OGRLineString crossingSource; crossingSource.addPoint(130.0, 132.0); crossingSource.addPoint(132.0, 130.0);
    crossingSourceFeature->SetGeometry(&crossingSource); noCrossSourceLineLayer->CreateFeature(crossingSourceFeature); OGRFeature::DestroyFeature(crossingSourceFeature);

    OGRLayer* containsPointLayer = ds->CreateLayer("ContainsPoints", &srs, wkbPoint, nullptr);
    OGRFeature* containedPointFeature = OGRFeature::CreateFeature(containsPointLayer->GetLayerDefn());
    OGRPoint containedPoint(140.5, 140.5); containedPointFeature->SetGeometry(&containedPoint); containsPointLayer->CreateFeature(containedPointFeature); OGRFeature::DestroyFeature(containedPointFeature);
    OGRFeature* containedPointFeature2 = OGRFeature::CreateFeature(containsPointLayer->GetLayerDefn());
    OGRPoint containedPoint2(140.75, 140.75); containedPointFeature2->SetGeometry(&containedPoint2); containsPointLayer->CreateFeature(containedPointFeature2); OGRFeature::DestroyFeature(containedPointFeature2);
    OGRLayer* containsPolygonLayer = ds->CreateLayer("ContainsPolygons", &srs, wkbPolygon, nullptr);
    OGRFeature* containsPolygonFeature = OGRFeature::CreateFeature(containsPolygonLayer->GetLayerDefn());
    OGRPolygon containsPolygon; OGRLinearRing containsRing; containsRing.addPoint(142.0, 140.0); containsRing.addPoint(143.0, 140.0); containsRing.addPoint(143.0, 141.0); containsRing.addPoint(142.0, 141.0); containsRing.addPoint(142.0, 140.0);
    containsPolygon.addRing(&containsRing); containsPolygonFeature->SetGeometry(&containsPolygon); containsPolygonLayer->CreateFeature(containsPolygonFeature); OGRFeature::DestroyFeature(containsPolygonFeature);

    OGRLayer* polygonBoundaryLineLayer = ds->CreateLayer("PolygonBoundaryLines", &srs, wkbLineString, nullptr);
    OGRFeature* polygonBoundaryLineFeature = OGRFeature::CreateFeature(polygonBoundaryLineLayer->GetLayerDefn());
    OGRLineString polygonBoundaryLine; polygonBoundaryLine.addPoint(150.0, 150.0); polygonBoundaryLine.addPoint(151.0, 150.0); polygonBoundaryLine.addPoint(151.0, 151.0); polygonBoundaryLine.addPoint(150.0, 151.0); polygonBoundaryLine.addPoint(150.0, 150.0);
    polygonBoundaryLineFeature->SetGeometry(&polygonBoundaryLine); polygonBoundaryLineLayer->CreateFeature(polygonBoundaryLineFeature); OGRFeature::DestroyFeature(polygonBoundaryLineFeature);
    OGRLayer* polygonBoundaryPolygonLayer = ds->CreateLayer("PolygonBoundaryPolygons", &srs, wkbPolygon, nullptr);
    OGRFeature* polygonBoundaryFeature = OGRFeature::CreateFeature(polygonBoundaryPolygonLayer->GetLayerDefn());
    OGRPolygon polygonBoundaryPolygon; OGRLinearRing polygonBoundaryRing; polygonBoundaryRing.addPoint(152.0, 150.0); polygonBoundaryRing.addPoint(153.0, 150.0); polygonBoundaryRing.addPoint(153.0, 151.0); polygonBoundaryRing.addPoint(152.0, 151.0); polygonBoundaryRing.addPoint(152.0, 150.0);
    polygonBoundaryPolygon.addRing(&polygonBoundaryRing); polygonBoundaryFeature->SetGeometry(&polygonBoundaryPolygon); polygonBoundaryPolygonLayer->CreateFeature(polygonBoundaryFeature); OGRFeature::DestroyFeature(polygonBoundaryFeature);

    OGRLayer* coverReferencePolygonLayer = ds->CreateLayer("CoverReferencePolygons", &srs, wkbPolygon, nullptr);
    OGRFeature* coverReferencePolygonFeature = OGRFeature::CreateFeature(coverReferencePolygonLayer->GetLayerDefn());
    OGRPolygon coverReferencePolygon; OGRLinearRing coverReferenceRing; coverReferenceRing.addPoint(160.0, 160.0); coverReferenceRing.addPoint(162.0, 160.0); coverReferenceRing.addPoint(162.0, 162.0); coverReferenceRing.addPoint(160.0, 162.0); coverReferenceRing.addPoint(160.0, 160.0);
    coverReferencePolygon.addRing(&coverReferenceRing); coverReferencePolygonFeature->SetGeometry(&coverReferencePolygon); coverReferencePolygonLayer->CreateFeature(coverReferencePolygonFeature); OGRFeature::DestroyFeature(coverReferencePolygonFeature);
    OGRLayer* coverSourcePolygonLayer = ds->CreateLayer("CoverSourcePolygons", &srs, wkbPolygon, nullptr);
    OGRFeature* uncoveredSourcePolygonFeature = OGRFeature::CreateFeature(coverSourcePolygonLayer->GetLayerDefn());
    OGRPolygon uncoveredSourcePolygon; OGRLinearRing uncoveredSourceRing; uncoveredSourceRing.addPoint(163.0, 160.0); uncoveredSourceRing.addPoint(164.0, 160.0); uncoveredSourceRing.addPoint(164.0, 161.0); uncoveredSourceRing.addPoint(163.0, 161.0); uncoveredSourceRing.addPoint(163.0, 160.0);
    uncoveredSourcePolygon.addRing(&uncoveredSourceRing); uncoveredSourcePolygonFeature->SetGeometry(&uncoveredSourcePolygon); coverSourcePolygonLayer->CreateFeature(uncoveredSourcePolygonFeature); OGRFeature::DestroyFeature(uncoveredSourcePolygonFeature);

    OGRLayer* noOverlapReferencePolygonLayer = ds->CreateLayer("NoOverlapReferencePolygons", &srs, wkbPolygon, nullptr);
    OGRFeature* noOverlapReferencePolygonFeature = OGRFeature::CreateFeature(noOverlapReferencePolygonLayer->GetLayerDefn());
    OGRPolygon noOverlapReferencePolygon; OGRLinearRing noOverlapReferenceRing; noOverlapReferenceRing.addPoint(170.0, 170.0); noOverlapReferenceRing.addPoint(172.0, 170.0); noOverlapReferenceRing.addPoint(172.0, 172.0); noOverlapReferenceRing.addPoint(170.0, 172.0); noOverlapReferenceRing.addPoint(170.0, 170.0);
    noOverlapReferencePolygon.addRing(&noOverlapReferenceRing); noOverlapReferencePolygonFeature->SetGeometry(&noOverlapReferencePolygon); noOverlapReferencePolygonLayer->CreateFeature(noOverlapReferencePolygonFeature); OGRFeature::DestroyFeature(noOverlapReferencePolygonFeature);
    OGRLayer* noOverlapSourcePolygonLayer = ds->CreateLayer("NoOverlapSourcePolygons", &srs, wkbPolygon, nullptr);
    OGRFeature* overlappingNoOverlapPolygonFeature = OGRFeature::CreateFeature(noOverlapSourcePolygonLayer->GetLayerDefn());
    OGRPolygon overlappingNoOverlapPolygon; OGRLinearRing overlappingNoOverlapRing; overlappingNoOverlapRing.addPoint(171.0, 171.0); overlappingNoOverlapRing.addPoint(173.0, 171.0); overlappingNoOverlapRing.addPoint(173.0, 173.0); overlappingNoOverlapRing.addPoint(171.0, 173.0); overlappingNoOverlapRing.addPoint(171.0, 171.0);
    overlappingNoOverlapPolygon.addRing(&overlappingNoOverlapRing); overlappingNoOverlapPolygonFeature->SetGeometry(&overlappingNoOverlapPolygon); noOverlapSourcePolygonLayer->CreateFeature(overlappingNoOverlapPolygonFeature); OGRFeature::DestroyFeature(overlappingNoOverlapPolygonFeature);

    OGRLayer* polygonBoundaryReferenceLayer = ds->CreateLayer("PolygonBoundaryReferencePolygons", &srs, wkbPolygon, nullptr);
    OGRFeature* polygonBoundaryReferenceFeature = OGRFeature::CreateFeature(polygonBoundaryReferenceLayer->GetLayerDefn());
    OGRPolygon polygonBoundaryReferencePolygon; OGRLinearRing polygonBoundaryReferenceRing; polygonBoundaryReferenceRing.addPoint(180.0, 180.0); polygonBoundaryReferenceRing.addPoint(181.0, 180.0); polygonBoundaryReferenceRing.addPoint(181.0, 181.0); polygonBoundaryReferenceRing.addPoint(180.0, 181.0); polygonBoundaryReferenceRing.addPoint(180.0, 180.0);
    polygonBoundaryReferencePolygon.addRing(&polygonBoundaryReferenceRing); polygonBoundaryReferenceFeature->SetGeometry(&polygonBoundaryReferencePolygon); polygonBoundaryReferenceLayer->CreateFeature(polygonBoundaryReferenceFeature); OGRFeature::DestroyFeature(polygonBoundaryReferenceFeature);
    OGRLayer* polygonBoundarySourceLayer = ds->CreateLayer("PolygonBoundarySourcePolygons", &srs, wkbPolygon, nullptr);
    OGRFeature* boundaryDifferentSourceFeature = OGRFeature::CreateFeature(polygonBoundarySourceLayer->GetLayerDefn());
    OGRPolygon boundaryDifferentSourcePolygon; OGRLinearRing boundaryDifferentSourceRing; boundaryDifferentSourceRing.addPoint(182.0, 180.0); boundaryDifferentSourceRing.addPoint(183.0, 180.0); boundaryDifferentSourceRing.addPoint(183.0, 181.0); boundaryDifferentSourceRing.addPoint(182.0, 181.0); boundaryDifferentSourceRing.addPoint(182.0, 180.0);
    boundaryDifferentSourcePolygon.addRing(&boundaryDifferentSourceRing); boundaryDifferentSourceFeature->SetGeometry(&boundaryDifferentSourcePolygon); polygonBoundarySourceLayer->CreateFeature(boundaryDifferentSourceFeature); OGRFeature::DestroyFeature(boundaryDifferentSourceFeature);

    GDALClose(ds);
}

int main() {
    try {
        configureGdalRuntime();
        GDALAllRegister();
        fs::path base = fs::u8path(u8"D:/jiedan/136/gis-qc-workbench/qa_synthetic_data");
        fs::remove_all(base);
        fs::create_directories(base);
        createCleanPackage(base / fs::u8path(u8"01_正常基础包"));
        createProblemPackage(base / fs::u8path(u8"02_缺失_命名_配套文件问题包"));
        createGeometryPackage(base / fs::u8path(u8"03_GPKG空间几何问题包"));
        std::cout << "generated " << base.u8string() << "\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "failed: " << ex.what() << "\n";
        return 1;
    }
}
