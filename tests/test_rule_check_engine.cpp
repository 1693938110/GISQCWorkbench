#include "core/RuleCheckEngine.h"
#include "core/RuleDefinition.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

#ifdef GISQC_HAVE_GDAL
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#endif

using namespace gisqc;

namespace fs = std::filesystem;

static RuleDefinition makeRule(std::string code, std::string severity = "error") {
    RuleDefinition r;
    r.code = std::move(code);
    r.name = "测试规则";
    r.category = "成果完整性";
    r.targetObject = "成果目录";
    r.enabled = true;
    r.severity = std::move(severity);
    if (r.code == "C030304") {
        r.name = "测试面伪结点规则";
        r.category = "空间专题";
        r.targetObject = "空间图层";
        r.message = "面不能有伪结点";
    } else if (r.code == "C030205") {
        r.name = "测试线连通性伪结点规则";
        r.category = "空间专题";
        r.targetObject = "空间图层";
        r.message = "线不能有连通性伪结点";
    } else {
        r.message = "规则发现问题";
    }
    return r;
}

static bool hasIssue(const std::vector<IssueRecord>& issues, const std::string& code, const std::string& featureId) {
    return std::any_of(issues.begin(), issues.end(), [&](const auto& issue) {
        return issue.ruleCode == code && issue.featureId == featureId && issue.description.find("[待实现]") == std::string::npos;
    });
}

static bool hasIssueWithDescription(const std::vector<IssueRecord>& issues,
                                    const std::string& code,
                                    const std::string& featureId,
                                    const std::string& descriptionFragment) {
    return std::any_of(issues.begin(), issues.end(), [&](const auto& issue) {
        return issue.ruleCode == code && issue.featureId == featureId &&
               issue.description.find("[待实现]") == std::string::npos &&
               issue.description.find(descriptionFragment) != std::string::npos;
    });
}

static void requireIssue(const std::vector<IssueRecord>& issues, const std::string& code, const std::string& featureId, const std::string& context) {
    if (!hasIssue(issues, code, featureId)) {
        std::cerr << "Missing expected issue " << code << " feature " << featureId << " (" << context << ")\n";
        for (const auto& issue : issues) {
            std::cerr << issue.ruleCode << " | " << issue.layerName << " | " << issue.featureId << " | " << issue.description << "\n";
        }
        std::exit(1);
    }
}

static void requireIssueDescription(const std::vector<IssueRecord>& issues,
                                    const std::string& code,
                                    const std::string& featureId,
                                    const std::string& descriptionFragment,
                                    const std::string& context) {
    if (!hasIssueWithDescription(issues, code, featureId, descriptionFragment)) {
        std::cerr << "Missing expected issue " << code << " feature " << featureId
                  << " containing '" << descriptionFragment << "' (" << context << ")\n";
        for (const auto& issue : issues) {
            std::cerr << issue.ruleCode << " | " << issue.layerName << " | " << issue.featureId << " | " << issue.description << "\n";
        }
        std::exit(1);
    }
}

#ifdef GISQC_HAVE_GDAL
static void addPolygonFeature(OGRLayer* layer, const std::vector<std::pair<double, double>>& ringPoints) {
    OGRFeature* feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
    OGRPolygon polygon;
    OGRLinearRing ring;
    for (const auto& point : ringPoints) {
        ring.addPoint(point.first, point.second);
    }
    polygon.addRing(&ring);
    feature->SetGeometry(&polygon);
    assert(layer->CreateFeature(feature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(feature);
}

static void addLineFeature(OGRLayer* layer, const std::vector<std::pair<double, double>>& points) {
    OGRFeature* feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
    OGRLineString line;
    for (const auto& point : points) {
        line.addPoint(point.first, point.second);
    }
    feature->SetGeometry(&line);
    assert(layer->CreateFeature(feature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(feature);
}

static void addPointFeature(OGRLayer* layer, double x, double y) {
    OGRFeature* feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
    OGRPoint point(x, y);
    feature->SetGeometry(&point);
    assert(layer->CreateFeature(feature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(feature);
}
#endif

static void writeDbfWithFields(const fs::path& path) {
    struct Field {
        std::string name;
        char type;
        unsigned char width;
        unsigned char decimals;
    };
    const std::vector<Field> fields = {
        {"NAME", 'C', 20, 0},
        {"CODE", 'N', 8, 0},
        {"EXTRA", 'C', 10, 0},
        {"STATUS", 'C', 2, 0},
        {"KIND", 'C', 2, 0},
    };
    const std::vector<std::vector<std::string>> records = {
        {"Alpha", "100", "E1", "A", "X"},
        {"Beta", "100", "E2", "", "X"},
        {"Gamma", "200", "E3", "C", "Y"},
        {"Delta", "300", "E4", "B", "Y"},
    };

    const std::uint16_t headerLength = static_cast<std::uint16_t>(32 + fields.size() * 32 + 1);
    std::uint16_t recordLength = 1;
    for (const auto& field : fields) {
        recordLength += field.width;
    }

    unsigned char header[32]{};
    header[0] = 0x03;
    header[1] = 0x7c;
    header[2] = 0x01;
    header[3] = 0x01;
    header[4] = static_cast<unsigned char>(records.size() & 0xff);
    header[5] = static_cast<unsigned char>((records.size() >> 8) & 0xff);
    header[6] = static_cast<unsigned char>((records.size() >> 16) & 0xff);
    header[7] = static_cast<unsigned char>((records.size() >> 24) & 0xff);
    header[8] = static_cast<unsigned char>(headerLength & 0xff);
    header[9] = static_cast<unsigned char>((headerLength >> 8) & 0xff);
    header[10] = static_cast<unsigned char>(recordLength & 0xff);
    header[11] = static_cast<unsigned char>((recordLength >> 8) & 0xff);

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    for (const auto& field : fields) {
        unsigned char descriptor[32]{};
        for (std::size_t i = 0; i < field.name.size() && i < 11; ++i) {
            descriptor[i] = static_cast<unsigned char>(field.name[i]);
        }
        descriptor[11] = static_cast<unsigned char>(field.type);
        descriptor[16] = field.width;
        descriptor[17] = field.decimals;
        out.write(reinterpret_cast<const char*>(descriptor), sizeof(descriptor));
    }
    const unsigned char terminator = 0x0D;
    out.write(reinterpret_cast<const char*>(&terminator), sizeof(terminator));

    for (const auto& record : records) {
        std::string row(static_cast<std::size_t>(recordLength), ' ');
        row[0] = ' ';
        std::size_t offset = 1;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto width = static_cast<std::size_t>(fields[i].width);
            const auto value = i < record.size() ? record[i] : "";
            row.replace(offset, std::min(width, value.size()), value.substr(0, width));
            offset += width;
        }
        out.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
}

#ifdef GISQC_HAVE_GDAL
static void createGeometryCheckPackage(const fs::path& path) {
    GDALAllRegister();
    fs::remove(path);
    fs::remove(fs::u8path(path.u8string() + "-journal"));
    fs::remove(fs::u8path(path.u8string() + "-wal"));
    fs::remove(fs::u8path(path.u8string() + "-shm"));
    auto* driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    assert(driver);
    GDALDataset* dataset = driver->Create(path.u8string().c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    assert(dataset);

    OGRSpatialReference srs;
    srs.importFromEPSG(4490);

    OGRLayer* lineLayer = dataset->CreateLayer("TinyLine", &srs, wkbLineString, nullptr);
    assert(lineLayer);
    OGRFeature* lineFeature = OGRFeature::CreateFeature(lineLayer->GetLayerDefn());
    OGRLineString tinyLine;
    tinyLine.addPoint(120.0, 30.0);
    tinyLine.addPoint(120.000001, 30.0);
    lineFeature->SetGeometry(&tinyLine);
    assert(lineLayer->CreateFeature(lineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(lineFeature);

    OGRLayer* zLayer = dataset->CreateLayer("ZPOINT", &srs, wkbPoint25D, nullptr);
    assert(zLayer);
    OGRFeature* zFeature = OGRFeature::CreateFeature(zLayer->GetLayerDefn());
    OGRPoint point(120.0, 30.0, 5.0);
    zFeature->SetGeometry(&point);
    assert(zLayer->CreateFeature(zFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(zFeature);

    OGRLayer* outOfRangeLayer = dataset->CreateLayer("OutOfRange", &srs, wkbPoint, nullptr);
    assert(outOfRangeLayer);
    OGRFeature* outOfRangeFeature = OGRFeature::CreateFeature(outOfRangeLayer->GetLayerDefn());
    OGRPoint outOfRangePoint(125.0, 35.0);
    outOfRangeFeature->SetGeometry(&outOfRangePoint);
    assert(outOfRangeLayer->CreateFeature(outOfRangeFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(outOfRangeFeature);

    OGRLayer* polyLayer = dataset->CreateLayer("PolyChecks", &srs, wkbPolygon, nullptr);
    assert(polyLayer);
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
    assert(polyLayer->CreateFeature(sharpFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(sharpFeature);

    OGRFeature* holeFeature = OGRFeature::CreateFeature(polyLayer->GetLayerDefn());
    OGRPolygon holePolygon;
    OGRLinearRing outer;
    outer.addPoint(10.0, 10.0);
    outer.addPoint(11.0, 10.0);
    outer.addPoint(11.0, 11.0);
    outer.addPoint(10.0, 11.0);
    outer.addPoint(10.0, 10.0);
    OGRLinearRing inner;
    inner.addPoint(10.2, 10.2);
    inner.addPoint(10.4, 10.2);
    inner.addPoint(10.4, 10.4);
    inner.addPoint(10.2, 10.4);
    inner.addPoint(10.2, 10.2);
    holePolygon.addRing(&outer);
    holePolygon.addRing(&inner);
    holeFeature->SetGeometry(&holePolygon);
    assert(polyLayer->CreateFeature(holeFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(holeFeature);

    OGRLayer* multiLineLayer = dataset->CreateLayer("MultiLine", &srs, wkbMultiLineString, nullptr);
    assert(multiLineLayer);
    OGRFeature* multiLineFeature = OGRFeature::CreateFeature(multiLineLayer->GetLayerDefn());
    OGRMultiLineString multiLine;
    OGRLineString partA;
    partA.addPoint(0.0, 0.0);
    partA.addPoint(1.0, 1.0);
    OGRLineString partB;
    partB.addPoint(2.0, 2.0);
    partB.addPoint(3.0, 3.0);
    multiLine.addGeometry(&partA);
    multiLine.addGeometry(&partB);
    multiLineFeature->SetGeometry(&multiLine);
    assert(multiLineLayer->CreateFeature(multiLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(multiLineFeature);

    OGRLayer* overlapLineLayer = dataset->CreateLayer("OverlapLines", &srs, wkbLineString, nullptr);
    assert(overlapLineLayer);
    OGRFeature* overlapA = OGRFeature::CreateFeature(overlapLineLayer->GetLayerDefn());
    OGRLineString overlapLineA;
    overlapLineA.addPoint(0.0, 20.0);
    overlapLineA.addPoint(2.0, 20.0);
    overlapA->SetGeometry(&overlapLineA);
    assert(overlapLineLayer->CreateFeature(overlapA) == OGRERR_NONE);
    OGRFeature::DestroyFeature(overlapA);
    OGRFeature* overlapB = OGRFeature::CreateFeature(overlapLineLayer->GetLayerDefn());
    OGRLineString overlapLineB;
    overlapLineB.addPoint(1.0, 20.0);
    overlapLineB.addPoint(3.0, 20.0);
    overlapB->SetGeometry(&overlapLineB);
    assert(overlapLineLayer->CreateFeature(overlapB) == OGRERR_NONE);
    OGRFeature::DestroyFeature(overlapB);

    OGRLayer* crossingLineLayer = dataset->CreateLayer("CrossingLines", &srs, wkbLineString, nullptr);
    assert(crossingLineLayer);
    OGRFeature* crossA = OGRFeature::CreateFeature(crossingLineLayer->GetLayerDefn());
    OGRLineString crossLineA;
    crossLineA.addPoint(0.0, 30.0);
    crossLineA.addPoint(2.0, 32.0);
    crossA->SetGeometry(&crossLineA);
    assert(crossingLineLayer->CreateFeature(crossA) == OGRERR_NONE);
    OGRFeature::DestroyFeature(crossA);
    OGRFeature* crossB = OGRFeature::CreateFeature(crossingLineLayer->GetLayerDefn());
    OGRLineString crossLineB;
    crossLineB.addPoint(0.0, 32.0);
    crossLineB.addPoint(2.0, 30.0);
    crossB->SetGeometry(&crossLineB);
    assert(crossingLineLayer->CreateFeature(crossB) == OGRERR_NONE);
    OGRFeature::DestroyFeature(crossB);

    OGRLayer* selfLineLayer = dataset->CreateLayer("SelfLine", &srs, wkbLineString, nullptr);
    assert(selfLineLayer);
    OGRFeature* selfLineFeature = OGRFeature::CreateFeature(selfLineLayer->GetLayerDefn());
    OGRLineString selfLine;
    selfLine.addPoint(0.0, 40.0);
    selfLine.addPoint(2.0, 42.0);
    selfLine.addPoint(0.0, 42.0);
    selfLine.addPoint(2.0, 40.0);
    selfLineFeature->SetGeometry(&selfLine);
    assert(selfLineLayer->CreateFeature(selfLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(selfLineFeature);

    OGRLayer* danglingLineLayer = dataset->CreateLayer("DanglingLines", &srs, wkbLineString, nullptr);
    assert(danglingLineLayer);
    OGRFeature* danglingFeature = OGRFeature::CreateFeature(danglingLineLayer->GetLayerDefn());
    OGRLineString danglingLine;
    danglingLine.addPoint(0.0, 50.0);
    danglingLine.addPoint(1.0, 50.0);
    danglingFeature->SetGeometry(&danglingLine);
    assert(danglingLineLayer->CreateFeature(danglingFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(danglingFeature);

    OGRLayer* pseudoNodeLayer = dataset->CreateLayer("PseudoNodeLines", &srs, wkbLineString, nullptr);
    assert(pseudoNodeLayer);
    OGRFeature* pseudoNodeFeature = OGRFeature::CreateFeature(pseudoNodeLayer->GetLayerDefn());
    OGRLineString pseudoNodeLine;
    pseudoNodeLine.addPoint(0.0, 60.0);
    pseudoNodeLine.addPoint(1.0, 60.0);
    pseudoNodeLine.addPoint(2.0, 60.0);
    pseudoNodeFeature->SetGeometry(&pseudoNodeLine);
    assert(pseudoNodeLayer->CreateFeature(pseudoNodeFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(pseudoNodeFeature);

    OGRLayer* connectivityPseudoNodeLayer = dataset->CreateLayer("ConnectivityPseudoNodeLines", &srs, wkbLineString, nullptr);
    assert(connectivityPseudoNodeLayer);
    OGRFeature* connectivityA = OGRFeature::CreateFeature(connectivityPseudoNodeLayer->GetLayerDefn());
    OGRLineString connectivityLineA;
    connectivityLineA.addPoint(0.0, 70.0);
    connectivityLineA.addPoint(1.0, 70.0);
    connectivityA->SetGeometry(&connectivityLineA);
    assert(connectivityPseudoNodeLayer->CreateFeature(connectivityA) == OGRERR_NONE);
    OGRFeature::DestroyFeature(connectivityA);
    OGRFeature* connectivityB = OGRFeature::CreateFeature(connectivityPseudoNodeLayer->GetLayerDefn());
    OGRLineString connectivityLineB;
    connectivityLineB.addPoint(1.0, 70.0);
    connectivityLineB.addPoint(2.0, 70.0);
    connectivityB->SetGeometry(&connectivityLineB);
    assert(connectivityPseudoNodeLayer->CreateFeature(connectivityB) == OGRERR_NONE);
    OGRFeature::DestroyFeature(connectivityB);

    OGRLayer* overlapPolyLayer = dataset->CreateLayer("OverlapPolys", &srs, wkbPolygon, nullptr);
    assert(overlapPolyLayer);
    OGRFeature* polyA = OGRFeature::CreateFeature(overlapPolyLayer->GetLayerDefn());
    OGRPolygon overlapPolyA;
    OGRLinearRing overlapOuterA;
    overlapOuterA.addPoint(20.0, 20.0);
    overlapOuterA.addPoint(22.0, 20.0);
    overlapOuterA.addPoint(22.0, 22.0);
    overlapOuterA.addPoint(20.0, 22.0);
    overlapOuterA.addPoint(20.0, 20.0);
    overlapPolyA.addRing(&overlapOuterA);
    polyA->SetGeometry(&overlapPolyA);
    assert(overlapPolyLayer->CreateFeature(polyA) == OGRERR_NONE);
    OGRFeature::DestroyFeature(polyA);
    OGRFeature* polyB = OGRFeature::CreateFeature(overlapPolyLayer->GetLayerDefn());
    OGRPolygon overlapPolyB;
    OGRLinearRing overlapOuterB;
    overlapOuterB.addPoint(21.0, 21.0);
    overlapOuterB.addPoint(23.0, 21.0);
    overlapOuterB.addPoint(23.0, 23.0);
    overlapOuterB.addPoint(21.0, 23.0);
    overlapOuterB.addPoint(21.0, 21.0);
    overlapPolyB.addRing(&overlapOuterB);
    polyB->SetGeometry(&overlapPolyB);
    assert(overlapPolyLayer->CreateFeature(polyB) == OGRERR_NONE);
    OGRFeature::DestroyFeature(polyB);

    OGRLayer* gapPolyLayer = dataset->CreateLayer("GapPolys", &srs, wkbPolygon, nullptr);
    assert(gapPolyLayer);
    OGRFeature* gapA = OGRFeature::CreateFeature(gapPolyLayer->GetLayerDefn());
    OGRPolygon gapPolyA;
    OGRLinearRing gapRingA;
    gapRingA.addPoint(30.0, 30.0);
    gapRingA.addPoint(31.0, 30.0);
    gapRingA.addPoint(31.0, 31.0);
    gapRingA.addPoint(30.0, 31.0);
    gapRingA.addPoint(30.0, 30.0);
    gapPolyA.addRing(&gapRingA);
    gapA->SetGeometry(&gapPolyA);
    assert(gapPolyLayer->CreateFeature(gapA) == OGRERR_NONE);
    OGRFeature::DestroyFeature(gapA);
    OGRFeature* gapB = OGRFeature::CreateFeature(gapPolyLayer->GetLayerDefn());
    OGRPolygon gapPolyB;
    OGRLinearRing gapRingB;
    gapRingB.addPoint(31.2, 30.0);
    gapRingB.addPoint(32.2, 30.0);
    gapRingB.addPoint(32.2, 31.0);
    gapRingB.addPoint(31.2, 31.0);
    gapRingB.addPoint(31.2, 30.0);
    gapPolyB.addRing(&gapRingB);
    gapB->SetGeometry(&gapPolyB);
    assert(gapPolyLayer->CreateFeature(gapB) == OGRERR_NONE);
    OGRFeature::DestroyFeature(gapB);

    OGRLayer* pseudoNodePolyLayer = dataset->CreateLayer("PseudoNodePolys", &srs, wkbPolygon, nullptr);
    assert(pseudoNodePolyLayer);
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
    assert(pseudoNodePolyLayer->CreateFeature(pseudoPolyFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(pseudoPolyFeature);

    OGRLayer* coincideRefPointLayer = dataset->CreateLayer("CoincideReferencePoints", &srs, wkbPoint, nullptr);
    assert(coincideRefPointLayer);
    addPointFeature(coincideRefPointLayer, 70.0, 80.0);
    OGRLayer* coincideSrcPointLayer = dataset->CreateLayer("CoincideSourcePoints", &srs, wkbPoint, nullptr);
    assert(coincideSrcPointLayer);
    addPointFeature(coincideSrcPointLayer, 70.0, 80.0);
    addPointFeature(coincideSrcPointLayer, 70.5, 80.0);

    OGRLayer* endpointRefLineLayer = dataset->CreateLayer("PointEndpointReferenceLines", &srs, wkbLineString, nullptr);
    assert(endpointRefLineLayer);
    addLineFeature(endpointRefLineLayer, {{72.0, 80.0}, {73.0, 80.0}});
    OGRLayer* endpointSrcPointLayer = dataset->CreateLayer("PointEndpointSourcePoints", &srs, wkbPoint, nullptr);
    assert(endpointSrcPointLayer);
    addPointFeature(endpointSrcPointLayer, 72.0, 80.0);
    addPointFeature(endpointSrcPointLayer, 72.5, 80.0);

    OGRLayer* pointLineRefLayer = dataset->CreateLayer("PointLineReferenceLines", &srs, wkbLineString, nullptr);
    assert(pointLineRefLayer);
    addLineFeature(pointLineRefLayer, {{74.0, 80.0}, {75.0, 80.0}});
    OGRLayer* pointLineSrcLayer = dataset->CreateLayer("PointLineSourcePoints", &srs, wkbPoint, nullptr);
    assert(pointLineSrcLayer);
    addPointFeature(pointLineSrcLayer, 74.5, 80.0);
    addPointFeature(pointLineSrcLayer, 74.5, 80.5);

    OGRLayer* pointBoundaryPolyLayer = dataset->CreateLayer("PointBoundaryPolys", &srs, wkbPolygon, nullptr);
    assert(pointBoundaryPolyLayer);
    addPolygonFeature(pointBoundaryPolyLayer, {{76.0, 80.0}, {77.0, 80.0}, {77.0, 81.0}, {76.0, 81.0}, {76.0, 80.0}});
    OGRLayer* pointBoundarySrcLayer = dataset->CreateLayer("PointBoundarySourcePoints", &srs, wkbPoint, nullptr);
    assert(pointBoundarySrcLayer);
    addPointFeature(pointBoundarySrcLayer, 76.0, 80.5);
    addPointFeature(pointBoundarySrcLayer, 76.5, 80.5);

    OGRLayer* pointInteriorPolyLayer = dataset->CreateLayer("PointInteriorPolys", &srs, wkbPolygon, nullptr);
    assert(pointInteriorPolyLayer);
    addPolygonFeature(pointInteriorPolyLayer, {{78.0, 80.0}, {79.0, 80.0}, {79.0, 81.0}, {78.0, 81.0}, {78.0, 80.0}});
    OGRLayer* pointInteriorSrcLayer = dataset->CreateLayer("PointInteriorSourcePoints", &srs, wkbPoint, nullptr);
    assert(pointInteriorSrcLayer);
    addPointFeature(pointInteriorSrcLayer, 78.5, 80.5);
    addPointFeature(pointInteriorSrcLayer, 78.0, 80.5);

    OGRLayer* boundaryPolyLayer = dataset->CreateLayer("BoundaryPolys", &srs, wkbPolygon, nullptr);
    assert(boundaryPolyLayer);
    OGRFeature* boundaryPolyFeature = OGRFeature::CreateFeature(boundaryPolyLayer->GetLayerDefn());
    OGRPolygon boundaryPolygon;
    OGRLinearRing boundaryRing;
    boundaryRing.addPoint(80.0, 80.0);
    boundaryRing.addPoint(82.0, 80.0);
    boundaryRing.addPoint(82.0, 82.0);
    boundaryRing.addPoint(80.0, 82.0);
    boundaryRing.addPoint(80.0, 80.0);
    boundaryPolygon.addRing(&boundaryRing);
    boundaryPolyFeature->SetGeometry(&boundaryPolygon);
    assert(boundaryPolyLayer->CreateFeature(boundaryPolyFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(boundaryPolyFeature);

    OGRLayer* boundaryLineLayer = dataset->CreateLayer("BoundaryLines", &srs, wkbLineString, nullptr);
    assert(boundaryLineLayer);
    OGRFeature* boundaryLineFeature = OGRFeature::CreateFeature(boundaryLineLayer->GetLayerDefn());
    OGRLineString boundaryLine;
    boundaryLine.addPoint(80.0, 80.0);
    boundaryLine.addPoint(82.0, 80.0);
    boundaryLineFeature->SetGeometry(&boundaryLine);
    assert(boundaryLineLayer->CreateFeature(boundaryLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(boundaryLineFeature);

    OGRFeature* offBoundaryLineFeature = OGRFeature::CreateFeature(boundaryLineLayer->GetLayerDefn());
    OGRLineString offBoundaryLine;
    offBoundaryLine.addPoint(80.0, 81.0);
    offBoundaryLine.addPoint(82.0, 81.0);
    offBoundaryLineFeature->SetGeometry(&offBoundaryLine);
    assert(boundaryLineLayer->CreateFeature(offBoundaryLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(offBoundaryLineFeature);

    OGRLayer* interiorPolyLayer = dataset->CreateLayer("InteriorPolys", &srs, wkbPolygon, nullptr);
    assert(interiorPolyLayer);
    OGRFeature* interiorPolyFeature = OGRFeature::CreateFeature(interiorPolyLayer->GetLayerDefn());
    OGRPolygon interiorPolygon;
    OGRLinearRing interiorRing;
    interiorRing.addPoint(90.0, 90.0);
    interiorRing.addPoint(92.0, 90.0);
    interiorRing.addPoint(92.0, 92.0);
    interiorRing.addPoint(90.0, 92.0);
    interiorRing.addPoint(90.0, 90.0);
    interiorPolygon.addRing(&interiorRing);
    interiorPolyFeature->SetGeometry(&interiorPolygon);
    assert(interiorPolyLayer->CreateFeature(interiorPolyFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(interiorPolyFeature);

    OGRLayer* interiorLineLayer = dataset->CreateLayer("InteriorLines", &srs, wkbLineString, nullptr);
    assert(interiorLineLayer);
    OGRFeature* interiorLineFeature = OGRFeature::CreateFeature(interiorLineLayer->GetLayerDefn());
    OGRLineString interiorLine;
    interiorLine.addPoint(90.2, 91.0);
    interiorLine.addPoint(91.8, 91.0);
    interiorLineFeature->SetGeometry(&interiorLine);
    assert(interiorLineLayer->CreateFeature(interiorLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(interiorLineFeature);

    OGRFeature* boundaryInteriorLineFeature = OGRFeature::CreateFeature(interiorLineLayer->GetLayerDefn());
    OGRLineString boundaryInteriorLine;
    boundaryInteriorLine.addPoint(90.0, 90.0);
    boundaryInteriorLine.addPoint(92.0, 90.0);
    boundaryInteriorLineFeature->SetGeometry(&boundaryInteriorLine);
    assert(interiorLineLayer->CreateFeature(boundaryInteriorLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(boundaryInteriorLineFeature);

    OGRLayer* endpointPointLayer = dataset->CreateLayer("EndpointPoints", &srs, wkbPoint, nullptr);
    assert(endpointPointLayer);
    OGRFeature* endpointA = OGRFeature::CreateFeature(endpointPointLayer->GetLayerDefn());
    OGRPoint endpointPointA(100.0, 100.0);
    endpointA->SetGeometry(&endpointPointA);
    assert(endpointPointLayer->CreateFeature(endpointA) == OGRERR_NONE);
    OGRFeature::DestroyFeature(endpointA);
    OGRFeature* endpointB = OGRFeature::CreateFeature(endpointPointLayer->GetLayerDefn());
    OGRPoint endpointPointB(101.0, 100.0);
    endpointB->SetGeometry(&endpointPointB);
    assert(endpointPointLayer->CreateFeature(endpointB) == OGRERR_NONE);
    OGRFeature::DestroyFeature(endpointB);

    OGRLayer* endpointLineLayer = dataset->CreateLayer("EndpointLines", &srs, wkbLineString, nullptr);
    assert(endpointLineLayer);
    OGRFeature* coveredEndpointLineFeature = OGRFeature::CreateFeature(endpointLineLayer->GetLayerDefn());
    OGRLineString coveredEndpointLine;
    coveredEndpointLine.addPoint(100.0, 100.0);
    coveredEndpointLine.addPoint(101.0, 100.0);
    coveredEndpointLineFeature->SetGeometry(&coveredEndpointLine);
    assert(endpointLineLayer->CreateFeature(coveredEndpointLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(coveredEndpointLineFeature);
    OGRFeature* missingEndpointLineFeature = OGRFeature::CreateFeature(endpointLineLayer->GetLayerDefn());
    OGRLineString missingEndpointLine;
    missingEndpointLine.addPoint(100.0, 100.0);
    missingEndpointLine.addPoint(102.0, 100.0);
    missingEndpointLineFeature->SetGeometry(&missingEndpointLine);
    assert(endpointLineLayer->CreateFeature(missingEndpointLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(missingEndpointLineFeature);

    OGRLayer* coverReferenceLineLayer = dataset->CreateLayer("CoverReferenceLines", &srs, wkbLineString, nullptr);
    assert(coverReferenceLineLayer);
    OGRFeature* coverReferenceLineFeature = OGRFeature::CreateFeature(coverReferenceLineLayer->GetLayerDefn());
    OGRLineString coverReferenceLine;
    coverReferenceLine.addPoint(110.0, 110.0);
    coverReferenceLine.addPoint(112.0, 110.0);
    coverReferenceLineFeature->SetGeometry(&coverReferenceLine);
    assert(coverReferenceLineLayer->CreateFeature(coverReferenceLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(coverReferenceLineFeature);

    OGRLayer* coverSourceLineLayer = dataset->CreateLayer("CoverSourceLines", &srs, wkbLineString, nullptr);
    assert(coverSourceLineLayer);
    OGRFeature* coveredSourceLineFeature = OGRFeature::CreateFeature(coverSourceLineLayer->GetLayerDefn());
    OGRLineString coveredSourceLine;
    coveredSourceLine.addPoint(110.2, 110.0);
    coveredSourceLine.addPoint(111.8, 110.0);
    coveredSourceLineFeature->SetGeometry(&coveredSourceLine);
    assert(coverSourceLineLayer->CreateFeature(coveredSourceLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(coveredSourceLineFeature);
    OGRFeature* uncoveredSourceLineFeature = OGRFeature::CreateFeature(coverSourceLineLayer->GetLayerDefn());
    OGRLineString uncoveredSourceLine;
    uncoveredSourceLine.addPoint(110.0, 110.2);
    uncoveredSourceLine.addPoint(112.0, 110.2);
    uncoveredSourceLineFeature->SetGeometry(&uncoveredSourceLine);
    assert(coverSourceLineLayer->CreateFeature(uncoveredSourceLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(uncoveredSourceLineFeature);

    OGRLayer* noOverlapReferenceLineLayer = dataset->CreateLayer("NoOverlapReferenceLines", &srs, wkbLineString, nullptr);
    assert(noOverlapReferenceLineLayer);
    OGRFeature* noOverlapReferenceLineFeature = OGRFeature::CreateFeature(noOverlapReferenceLineLayer->GetLayerDefn());
    OGRLineString noOverlapReferenceLine;
    noOverlapReferenceLine.addPoint(120.0, 120.0);
    noOverlapReferenceLine.addPoint(122.0, 120.0);
    noOverlapReferenceLineFeature->SetGeometry(&noOverlapReferenceLine);
    assert(noOverlapReferenceLineLayer->CreateFeature(noOverlapReferenceLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(noOverlapReferenceLineFeature);

    OGRLayer* noOverlapSourceLineLayer = dataset->CreateLayer("NoOverlapSourceLines", &srs, wkbLineString, nullptr);
    assert(noOverlapSourceLineLayer);
    OGRFeature* disjointNoOverlapSourceFeature = OGRFeature::CreateFeature(noOverlapSourceLineLayer->GetLayerDefn());
    OGRLineString disjointNoOverlapSource;
    disjointNoOverlapSource.addPoint(120.0, 121.0);
    disjointNoOverlapSource.addPoint(122.0, 121.0);
    disjointNoOverlapSourceFeature->SetGeometry(&disjointNoOverlapSource);
    assert(noOverlapSourceLineLayer->CreateFeature(disjointNoOverlapSourceFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(disjointNoOverlapSourceFeature);
    OGRFeature* overlappingNoOverlapSourceFeature = OGRFeature::CreateFeature(noOverlapSourceLineLayer->GetLayerDefn());
    OGRLineString overlappingNoOverlapSource;
    overlappingNoOverlapSource.addPoint(121.0, 120.0);
    overlappingNoOverlapSource.addPoint(123.0, 120.0);
    overlappingNoOverlapSourceFeature->SetGeometry(&overlappingNoOverlapSource);
    assert(noOverlapSourceLineLayer->CreateFeature(overlappingNoOverlapSourceFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(overlappingNoOverlapSourceFeature);

    OGRLayer* noCrossReferenceLineLayer = dataset->CreateLayer("NoCrossReferenceLines", &srs, wkbLineString, nullptr);
    assert(noCrossReferenceLineLayer);
    OGRFeature* noCrossReferenceLineFeature = OGRFeature::CreateFeature(noCrossReferenceLineLayer->GetLayerDefn());
    OGRLineString noCrossReferenceLine;
    noCrossReferenceLine.addPoint(130.0, 130.0);
    noCrossReferenceLine.addPoint(132.0, 132.0);
    noCrossReferenceLineFeature->SetGeometry(&noCrossReferenceLine);
    assert(noCrossReferenceLineLayer->CreateFeature(noCrossReferenceLineFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(noCrossReferenceLineFeature);

    OGRLayer* noCrossSourceLineLayer = dataset->CreateLayer("NoCrossSourceLines", &srs, wkbLineString, nullptr);
    assert(noCrossSourceLineLayer);
    OGRFeature* sharedEndpointSourceFeature = OGRFeature::CreateFeature(noCrossSourceLineLayer->GetLayerDefn());
    OGRLineString sharedEndpointSource;
    sharedEndpointSource.addPoint(132.0, 132.0);
    sharedEndpointSource.addPoint(133.0, 132.0);
    sharedEndpointSourceFeature->SetGeometry(&sharedEndpointSource);
    assert(noCrossSourceLineLayer->CreateFeature(sharedEndpointSourceFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(sharedEndpointSourceFeature);
    OGRFeature* crossingSourceFeature = OGRFeature::CreateFeature(noCrossSourceLineLayer->GetLayerDefn());
    OGRLineString crossingSource;
    crossingSource.addPoint(130.0, 132.0);
    crossingSource.addPoint(132.0, 130.0);
    crossingSourceFeature->SetGeometry(&crossingSource);
    assert(noCrossSourceLineLayer->CreateFeature(crossingSourceFeature) == OGRERR_NONE);
    OGRFeature::DestroyFeature(crossingSourceFeature);

    OGRLayer* containsPointLayer = dataset->CreateLayer("ContainsPoints", &srs, wkbPoint, nullptr);
    assert(containsPointLayer);
    addPointFeature(containsPointLayer, 140.5, 140.5);
    addPointFeature(containsPointLayer, 140.75, 140.75);
    OGRLayer* containsPolygonLayer = dataset->CreateLayer("ContainsPolygons", &srs, wkbPolygon, nullptr);
    assert(containsPolygonLayer);
    addPolygonFeature(containsPolygonLayer, {{140.0, 140.0}, {141.0, 140.0}, {141.0, 141.0}, {140.0, 141.0}, {140.0, 140.0}});
    addPolygonFeature(containsPolygonLayer, {{142.0, 140.0}, {143.0, 140.0}, {143.0, 141.0}, {142.0, 141.0}, {142.0, 140.0}});

    OGRLayer* polygonBoundaryLineLayer = dataset->CreateLayer("PolygonBoundaryLines", &srs, wkbLineString, nullptr);
    assert(polygonBoundaryLineLayer);
    addLineFeature(polygonBoundaryLineLayer, {{150.0, 150.0}, {151.0, 150.0}, {151.0, 151.0}, {150.0, 151.0}, {150.0, 150.0}});
    OGRLayer* polygonBoundaryPolygonLayer = dataset->CreateLayer("PolygonBoundaryPolygons", &srs, wkbPolygon, nullptr);
    assert(polygonBoundaryPolygonLayer);
    addPolygonFeature(polygonBoundaryPolygonLayer, {{150.0, 150.0}, {151.0, 150.0}, {151.0, 151.0}, {150.0, 151.0}, {150.0, 150.0}});
    addPolygonFeature(polygonBoundaryPolygonLayer, {{152.0, 150.0}, {153.0, 150.0}, {153.0, 151.0}, {152.0, 151.0}, {152.0, 150.0}});

    OGRLayer* coverReferencePolygonLayer = dataset->CreateLayer("CoverReferencePolygons", &srs, wkbPolygon, nullptr);
    assert(coverReferencePolygonLayer);
    addPolygonFeature(coverReferencePolygonLayer, {{160.0, 160.0}, {162.0, 160.0}, {162.0, 162.0}, {160.0, 162.0}, {160.0, 160.0}});
    OGRLayer* coverSourcePolygonLayer = dataset->CreateLayer("CoverSourcePolygons", &srs, wkbPolygon, nullptr);
    assert(coverSourcePolygonLayer);
    addPolygonFeature(coverSourcePolygonLayer, {{160.2, 160.2}, {160.8, 160.2}, {160.8, 160.8}, {160.2, 160.8}, {160.2, 160.2}});
    addPolygonFeature(coverSourcePolygonLayer, {{163.0, 160.0}, {164.0, 160.0}, {164.0, 161.0}, {163.0, 161.0}, {163.0, 160.0}});

    OGRLayer* noOverlapReferencePolygonLayer = dataset->CreateLayer("NoOverlapReferencePolygons", &srs, wkbPolygon, nullptr);
    assert(noOverlapReferencePolygonLayer);
    addPolygonFeature(noOverlapReferencePolygonLayer, {{170.0, 170.0}, {172.0, 170.0}, {172.0, 172.0}, {170.0, 172.0}, {170.0, 170.0}});
    OGRLayer* noOverlapSourcePolygonLayer = dataset->CreateLayer("NoOverlapSourcePolygons", &srs, wkbPolygon, nullptr);
    assert(noOverlapSourcePolygonLayer);
    addPolygonFeature(noOverlapSourcePolygonLayer, {{173.0, 170.0}, {174.0, 170.0}, {174.0, 171.0}, {173.0, 171.0}, {173.0, 170.0}});
    addPolygonFeature(noOverlapSourcePolygonLayer, {{171.0, 171.0}, {173.0, 171.0}, {173.0, 173.0}, {171.0, 173.0}, {171.0, 171.0}});

    OGRLayer* polygonBoundaryReferenceLayer = dataset->CreateLayer("PolygonBoundaryReferencePolygons", &srs, wkbPolygon, nullptr);
    assert(polygonBoundaryReferenceLayer);
    addPolygonFeature(polygonBoundaryReferenceLayer, {{180.0, 180.0}, {181.0, 180.0}, {181.0, 181.0}, {180.0, 181.0}, {180.0, 180.0}});
    OGRLayer* polygonBoundarySourceLayer = dataset->CreateLayer("PolygonBoundarySourcePolygons", &srs, wkbPolygon, nullptr);
    assert(polygonBoundarySourceLayer);
    addPolygonFeature(polygonBoundarySourceLayer, {{180.0, 180.0}, {181.0, 180.0}, {181.0, 181.0}, {180.0, 181.0}, {180.0, 180.0}});
    addPolygonFeature(polygonBoundarySourceLayer, {{182.0, 180.0}, {183.0, 180.0}, {183.0, 181.0}, {182.0, 181.0}, {182.0, 180.0}});

    GDALClose(dataset);
}
#endif

int main() {
    const fs::path root = fs::temp_directory_path() / "gis_qc_rule_check_engine_test";
    fs::remove_all(root);
    fs::create_directories(root / fs::u8path(u8"空间数据"));
    fs::create_directories(root / fs::u8path(u8"空目录"));
    std::ofstream(root / fs::u8path(u8"readme.txt")) << "说明";
    std::ofstream(root / fs::u8path(u8"bad name.shp")) << "placeholder";
    std::ofstream(root / fs::u8path(u8"GXDX_GD.shp")) << "placeholder";
    std::ofstream(root / fs::u8path(u8"GXDX_GD.shx")) << "placeholder";

    RuleCheckEngine engine;

    // A010101: template directory comparison
    const fs::path templateDir = fs::temp_directory_path() / "gis_qc_template_test";
    fs::remove_all(templateDir);
    fs::create_directories(templateDir / fs::u8path(u8"\u7a7a\u95f4\u6570\u636e"));
    fs::create_directories(templateDir / fs::u8path(u8"\u6587\u6863\u8d44\u6599"));
    fs::create_directories(templateDir / fs::u8path(u8"\u5143\u6570\u636e"));
    std::ofstream(templateDir / fs::u8path(u8"readme.txt")) << "template";
    std::ofstream(templateDir / fs::u8path(u8"metadata.xml")) << "template";

    auto templateDirRule = makeRule("A010101");
    templateDirRule.parameters["templateDir"] = templateDir.u8string();

    auto layerRule = makeRule("B010202");
    layerRule.parameters["requiredTables"] = "GXDX_GD,MissingLayer";

    auto datasetMatchRule = makeRule("B010101", "warning");
    datasetMatchRule.parameters["allowedDatasets"] = "GXDX_GD,SchemaLayer";

    auto requiredDatasetRule = makeRule("B010102");
    requiredDatasetRule.parameters["requiredTables"] = "GXDX_GD,MissingDataset";

    auto tableSpecRule = makeRule("B020101", "warning");
    tableSpecRule.parameters["allowedTables"] = "GXDX_GD,SchemaLayer";

    auto tableAliasTypeRule = makeRule("B010203", "warning");
    tableAliasTypeRule.parameters["tableAliasTypes"] = "GXDX_GD=管点表;SchemaLayer=结构测试表";

    auto tableDatasetRule = makeRule("B010204", "warning");
    tableDatasetRule.parameters["tableDatasets"] = "GXDX_GD=管线数据;SchemaLayer=属性测试";

    auto crsRule = makeRule("C010201");
    crsRule.parameters["wkid"] = "4490";
    crsRule.parameters["name"] = "CGCS2000";

    const auto issues = engine.check(root.u8string(), {
        templateDirRule,
        layerRule, datasetMatchRule, requiredDatasetRule, tableSpecRule, tableAliasTypeRule, tableDatasetRule, crsRule
    });

    // Template has: 空间数据/, 文档资料/, 元数据/, readme.txt, metadata.xml
    // Target has: 空间数据/, 空目录/, readme.txt, bad name.shp, GXDX_GD.shp, GXDX_GD.shx
    // Missing: 文档资料, 元数据, metadata.xml
    // Extra: 空目录, bad name.shp, GXDX_GD.shp, GXDX_GD.shx
    assert(hasIssue(issues, "A010101", "metadata.xml"));
    assert(hasIssue(issues, "B010202", "MissingLayer"));
    assert(hasIssue(issues, "B010101", "bad name"));
    assert(hasIssue(issues, "B010102", "MissingDataset"));
    assert(hasIssue(issues, "B020101", "bad name"));
    assert(hasIssue(issues, "B010203", "bad name"));
    assert(hasIssue(issues, "B010204", "bad name"));
    assert(!hasIssue(issues, "B010202", "GXDX_GD"));
    assert(hasIssue(issues, "C010201", "GXDX_GD.shp"));

    fs::remove(root / fs::u8path(u8"bad name.shp"));
    std::ofstream(root / fs::u8path(u8"GXDX_GD.dbf")) << "placeholder";
    std::ofstream(root / fs::u8path(u8"GXDX_GD.prj")) << "GEOGCS[\"CGCS2000\",AUTHORITY[\"EPSG\",\"4490\"]]";
    fs::create_directories(root / fs::u8path(u8"文档资料"));
    fs::create_directories(root / fs::u8path(u8"元数据"));
    std::ofstream(root / fs::u8path(u8"metadata.xml")) << "<metadata/>";
    std::ofstream(root / fs::u8path(u8"空目录") / "keep.txt") << "x";
    std::ofstream(root / fs::u8path(u8"空间数据") / "keep.txt") << "x";
    std::ofstream(root / fs::u8path(u8"文档资料") / "keep.txt") << "x";
    std::ofstream(root / fs::u8path(u8"元数据") / "keep.txt") << "x";

    layerRule.parameters["requiredTables"] = "GXDX_GD";
    const auto fixedIssues = engine.check(root.u8string(), {templateDirRule, layerRule, crsRule});
    // After fixing the structure, template comparison should only show extras (空目录/keep.txt etc.)
    // But layerRule + crsRule should pass now
    assert(!hasIssue(fixedIssues, "B010202", "GXDX_GD"));

    std::ofstream(root / fs::u8path(u8"SchemaLayer.shp")) << "placeholder";
    std::ofstream(root / fs::u8path(u8"SchemaLayer.shx")) << "placeholder";
    writeDbfWithFields(root / fs::u8path(u8"SchemaLayer.dbf"));

    auto fieldNameRule = makeRule("B020102");
    fieldNameRule.parameters["requiredFields"] = "SchemaLayer.NAME,SchemaLayer.MISSING";
    fieldNameRule.parameters["allowedFields"] = "SchemaLayer.NAME,SchemaLayer.CODE";

    auto fieldTypeRule = makeRule("B020103", "warning");
    fieldTypeRule.parameters["fieldTypes"] = "SchemaLayer.NAME=text;SchemaLayer.CODE=text";

    auto fieldLengthRule = makeRule("B020104", "warning");
    fieldLengthRule.parameters["fieldLengths"] = "SchemaLayer.NAME=30;SchemaLayer.CODE=8";

    const auto schemaIssues = engine.check(root.u8string(), {fieldNameRule, fieldTypeRule, fieldLengthRule});
    assert(hasIssue(schemaIssues, "B020102", "MISSING"));
    assert(hasIssue(schemaIssues, "B020102", "EXTRA"));
    assert(hasIssue(schemaIssues, "B020103", "CODE"));
    assert(hasIssue(schemaIssues, "B020104", "NAME"));
    assert(!hasIssue(schemaIssues, "B020104", "CODE"));

    auto domainRule = makeRule("B020106", "warning");
    domainRule.parameters["valueDomains"] = "SchemaLayer.STATUS=A|B";

    auto uniqueRule = makeRule("B020107", "warning");
    uniqueRule.parameters["uniqueFields"] = "SchemaLayer.CODE";

    auto requiredValueRule = makeRule("B020108");
    requiredValueRule.parameters["requiredValueFields"] = "SchemaLayer.STATUS";

    auto singleEnumRule = makeRule("B020201", "warning");
    singleEnumRule.parameters["singleFieldEnums"] = "SchemaLayer.KIND=X";

    auto multiEnumRule = makeRule("B020202", "warning");
    multiEnumRule.parameters["multiFieldEnums"] = "SchemaLayer.STATUS+KIND=A|X,B|X,C|Y";

    const auto valueIssues = engine.check(root.u8string(), {domainRule, uniqueRule, requiredValueRule, singleEnumRule, multiEnumRule});
    assert(hasIssue(valueIssues, "B020106", "3"));
    assert(hasIssue(valueIssues, "B020107", "2"));
    assert(hasIssue(valueIssues, "B020108", "2"));
    assert(hasIssue(valueIssues, "B020201", "3"));
    assert(hasIssue(valueIssues, "B020202", "4"));

#ifdef GISQC_HAVE_GDAL
    createGeometryCheckPackage(root / fs::u8path(u8"geometry_checks.gpkg"));
    auto shortLineRule = makeRule("C020201", "warning");
    shortLineRule.parameters["layers"] = "TinyLine";
    shortLineRule.parameters["minLength"] = "1";
    auto rangeRule = makeRule("C010101");
    rangeRule.parameters["layers"] = "OutOfRange";
    rangeRule.parameters["minX"] = "119";
    rangeRule.parameters["minY"] = "29";
    rangeRule.parameters["maxX"] = "121";
    rangeRule.parameters["maxY"] = "31";
    auto zRule = makeRule("C020501");
    zRule.parameters["layers"] = "ZPOINT";
    auto shortEdgeRule = makeRule("C020301", "warning");
    shortEdgeRule.parameters["layers"] = "PolyChecks";
    shortEdgeRule.parameters["minEdgeLength"] = "0.02";
    auto sharpAngleRule = makeRule("C020302", "warning");
    sharpAngleRule.parameters["layers"] = "PolyChecks";
    sharpAngleRule.parameters["angleTolerance"] = "30";
    auto nodeRule = makeRule("C020401", "warning");
    nodeRule.parameters["layers"] = "TinyLine";
    nodeRule.parameters["minNodes"] = "3";
    auto singleLineRule = makeRule("C030208");
    singleLineRule.parameters["layers"] = "MultiLine";
    auto holeRule = makeRule("C030305");
    holeRule.parameters["layers"] = "PolyChecks";
    auto lineOverlapRule = makeRule("C030201");
    lineOverlapRule.parameters["layers"] = "OverlapLines";
    auto lineIntersectRule = makeRule("C030202");
    lineIntersectRule.parameters["layers"] = "CrossingLines";
    auto lineSelfOverlapRule = makeRule("C030206");
    lineSelfOverlapRule.parameters["layers"] = "SelfLine";
    auto lineSelfIntersectRule = makeRule("C030207");
    lineSelfIntersectRule.parameters["layers"] = "SelfLine";
    auto danglingPointRule = makeRule("C030203");
    danglingPointRule.parameters["layers"] = "DanglingLines";
    auto pseudoNodeRule = makeRule("C030204");
    pseudoNodeRule.parameters["layers"] = "PseudoNodeLines";
    auto connectivityPseudoNodeRule = makeRule("C030205");
    connectivityPseudoNodeRule.parameters["layers"] = "ConnectivityPseudoNodeLines";
    auto polygonOverlapRule = makeRule("C030302");
    polygonOverlapRule.parameters["layers"] = "OverlapPolys";
    polygonOverlapRule.parameters["areaTolerance"] = "0.0001";
    auto polygonGapRule = makeRule("C030301");
    polygonGapRule.parameters["layers"] = "GapPolys";
    polygonGapRule.parameters["gapTolerance"] = "0.05";
    auto polygonPseudoNodeRule = makeRule("C030304");
    polygonPseudoNodeRule.parameters["layers"] = "PseudoNodePolys";
    auto pointCoincideRule = makeRule("C030103");
    pointCoincideRule.parameters["sourceLayer"] = "CoincideSourcePoints";
    pointCoincideRule.parameters["referenceLayer"] = "CoincideReferencePoints";
    auto pointLineEndpointRule = makeRule("C030104");
    pointLineEndpointRule.parameters["sourceLayer"] = "PointEndpointSourcePoints";
    pointLineEndpointRule.parameters["referenceLayer"] = "PointEndpointReferenceLines";
    auto pointOnLineRule = makeRule("C030105");
    pointOnLineRule.parameters["sourceLayer"] = "PointLineSourcePoints";
    pointOnLineRule.parameters["referenceLayer"] = "PointLineReferenceLines";
    auto pointOnPolygonBoundaryRule = makeRule("C030106");
    pointOnPolygonBoundaryRule.parameters["sourceLayer"] = "PointBoundarySourcePoints";
    pointOnPolygonBoundaryRule.parameters["referenceLayer"] = "PointBoundaryPolys";
    auto pointInsidePolygonRule = makeRule("C030107");
    pointInsidePolygonRule.parameters["sourceLayer"] = "PointInteriorSourcePoints";
    pointInsidePolygonRule.parameters["referenceLayer"] = "PointInteriorPolys";
    auto lineOnBoundaryRule = makeRule("C030215");
    lineOnBoundaryRule.parameters["sourceLayer"] = "BoundaryLines";
    lineOnBoundaryRule.parameters["referenceLayer"] = "BoundaryPolys";
    auto lineInsidePolygonRule = makeRule("C030216");
    lineInsidePolygonRule.parameters["sourceLayer"] = "InteriorLines";
    lineInsidePolygonRule.parameters["referenceLayer"] = "InteriorPolys";
    auto lineEndpointPointRule = makeRule("C030210");
    lineEndpointPointRule.parameters["sourceLayer"] = "EndpointLines";
    lineEndpointPointRule.parameters["referenceLayer"] = "EndpointPoints";
    auto lineCoveredByLineRule = makeRule("C030211");
    lineCoveredByLineRule.parameters["sourceLayer"] = "CoverSourceLines";
    lineCoveredByLineRule.parameters["referenceLayer"] = "CoverReferenceLines";
    auto lineNotOverlapLineRule = makeRule("C030212");
    lineNotOverlapLineRule.parameters["sourceLayer"] = "NoOverlapSourceLines";
    lineNotOverlapLineRule.parameters["referenceLayer"] = "NoOverlapReferenceLines";
    auto lineNotCrossLineRule = makeRule("C030213");
    lineNotCrossLineRule.parameters["sourceLayer"] = "NoCrossSourceLines";
    lineNotCrossLineRule.parameters["referenceLayer"] = "NoCrossReferenceLines";
    auto polygonContainsPointRule = makeRule("C030307");
    polygonContainsPointRule.parameters["sourceLayer"] = "ContainsPolygons";
    polygonContainsPointRule.parameters["referenceLayer"] = "ContainsPoints";
    auto polygonContainsOnePointRule = makeRule("C030308");
    polygonContainsOnePointRule.parameters["sourceLayer"] = "ContainsPolygons";
    polygonContainsOnePointRule.parameters["referenceLayer"] = "ContainsPoints";
    auto polygonBoundaryCoveredByLineRule = makeRule("C030309");
    polygonBoundaryCoveredByLineRule.parameters["sourceLayer"] = "PolygonBoundaryPolygons";
    polygonBoundaryCoveredByLineRule.parameters["referenceLayer"] = "PolygonBoundaryLines";
    auto polygonCoveredByPolygonRule = makeRule("C030310");
    polygonCoveredByPolygonRule.parameters["sourceLayer"] = "CoverSourcePolygons";
    polygonCoveredByPolygonRule.parameters["referenceLayer"] = "CoverReferencePolygons";
    auto polygonMutualCoveredByPolygonRule = makeRule("C030312");
    polygonMutualCoveredByPolygonRule.parameters["sourceLayer"] = "CoverSourcePolygons";
    polygonMutualCoveredByPolygonRule.parameters["referenceLayer"] = "CoverReferencePolygons";
    auto polygonNoOverlapPolygonRule = makeRule("C030313");
    polygonNoOverlapPolygonRule.parameters["sourceLayer"] = "NoOverlapSourcePolygons";
    polygonNoOverlapPolygonRule.parameters["referenceLayer"] = "NoOverlapReferencePolygons";
    auto polygonBoundaryCoveredByPolygonRule = makeRule("C030314");
    polygonBoundaryCoveredByPolygonRule.parameters["sourceLayer"] = "PolygonBoundarySourcePolygons";
    polygonBoundaryCoveredByPolygonRule.parameters["referenceLayer"] = "PolygonBoundaryReferencePolygons";
    const auto geometryIssues = engine.check(root.u8string(), {
        shortLineRule, rangeRule, zRule, shortEdgeRule, sharpAngleRule, nodeRule, singleLineRule, holeRule,
        lineOverlapRule, lineIntersectRule, lineSelfOverlapRule, lineSelfIntersectRule,
        danglingPointRule, pseudoNodeRule, connectivityPseudoNodeRule, polygonOverlapRule, polygonGapRule, polygonPseudoNodeRule,
        pointCoincideRule, pointLineEndpointRule, pointOnLineRule, pointOnPolygonBoundaryRule, pointInsidePolygonRule,
        lineOnBoundaryRule, lineInsidePolygonRule, lineEndpointPointRule, lineCoveredByLineRule, lineNotOverlapLineRule, lineNotCrossLineRule,
        polygonContainsPointRule, polygonContainsOnePointRule, polygonBoundaryCoveredByLineRule, polygonCoveredByPolygonRule,
        polygonMutualCoveredByPolygonRule, polygonNoOverlapPolygonRule, polygonBoundaryCoveredByPolygonRule
    });
    requireIssue(geometryIssues, "C020201", "1", "tiny line length below tolerance");
    requireIssue(geometryIssues, "C010101", "1", "point extent outside configured working range");
    requireIssue(geometryIssues, "C020501", "1", "3D point contains Z value");
    requireIssue(geometryIssues, "C020301", "1", "polygon has short edge");
    requireIssue(geometryIssues, "C020302", "1", "polygon has sharp angle");
    requireIssue(geometryIssues, "C020401", "1", "line has fewer nodes than minimum");
    requireIssue(geometryIssues, "C030208", "1", "multi-line must be single-part");
    requireIssue(geometryIssues, "C030305", "2", "polygon hole is forbidden");
    requireIssue(geometryIssues, "C030201", "2", "line overlap with same layer feature");
    requireIssue(geometryIssues, "C030202", "2", "line intersects another same-layer feature");
    requireIssue(geometryIssues, "C030206", "1", "line self-overlap check");
    requireIssue(geometryIssues, "C030207", "1", "line self-intersection check");
    requireIssue(geometryIssues, "C030203", "1", "standalone line has dangling endpoints");
    requireIssue(geometryIssues, "C030204", "1", "collinear middle vertex is a pseudo node");
    requireIssueDescription(geometryIssues, "C030205", "2", "端点仅连接一条其他线", "line endpoint connects to exactly one other line");
    requireIssue(geometryIssues, "C030302", "2", "polygon overlap with same layer feature");
    requireIssue(geometryIssues, "C030301", "2", "gap exists between same-layer polygons");
    requireIssue(geometryIssues, "C030304", "1", "polygon ring contains a pseudo node");
    requireIssueDescription(geometryIssues, "C030103", "2", "点要素未与参照图层的点重合", "point does not coincide with reference point");
    requireIssueDescription(geometryIssues, "C030104", "2", "点要素未被参照线端点覆盖", "point is not covered by reference line endpoint");
    requireIssueDescription(geometryIssues, "C030105", "2", "点要素未被参照线图层覆盖", "point is not covered by reference line");
    requireIssueDescription(geometryIssues, "C030106", "2", "要素未位于参照图层边界上", "point is not on reference polygon boundary");
    requireIssueDescription(geometryIssues, "C030107", "2", "要素未位于参照图层面的内部", "point is not strictly inside reference polygon");
    requireIssueDescription(geometryIssues, "C030215", "2", "线要素未被参照面图层边界覆盖", "line not covered by polygon boundary");
    requireIssueDescription(geometryIssues, "C030216", "2", "线要素未严格位于参照图层面的内部", "boundary line is not strictly inside polygon interior");
    requireIssueDescription(geometryIssues, "C030210", "2", "线要素端点未被参照点图层覆盖", "line endpoint missing point coverage");
    requireIssueDescription(geometryIssues, "C030211", "2", "线要素未被参照线图层覆盖", "line not covered by reference line layer");
    requireIssueDescription(geometryIssues, "C030212", "2", "线要素与参照线图层存在重叠", "line overlaps reference line layer");
    requireIssueDescription(geometryIssues, "C030213", "2", "线要素与参照线图层存在相交", "line crosses reference line layer");
    requireIssueDescription(geometryIssues, "C030307", "2", "面要素未包含参照图层的点", "polygon does not contain a reference point");
    requireIssueDescription(geometryIssues, "C030308", "1", "面要素未严格包含一个参照图层的点", "polygon contains more than one reference point");
    requireIssueDescription(geometryIssues, "C030308", "2", "面要素未严格包含一个参照图层的点", "polygon does not contain exactly one reference point");
    requireIssueDescription(geometryIssues, "C030309", "2", "面边界未被参照线图层覆盖", "polygon boundary is not covered by reference lines");
    requireIssueDescription(geometryIssues, "C030310", "2", "面要素未被参照图层的面覆盖", "polygon is not covered by reference polygons");
    requireIssueDescription(geometryIssues, "C030312", "2", "面要素未与参照图层的面相互覆盖", "polygon is not mutually covered by reference polygons");
    requireIssueDescription(geometryIssues, "C030313", "2", "面要素与参照图层存在重叠", "polygon overlaps reference polygons");
    requireIssueDescription(geometryIssues, "C030314", "2", "面边界未被参照面图层的边界覆盖", "polygon boundary is not covered by reference polygon boundaries");
#endif

    auto disabled = templateDirRule;
    disabled.enabled = false;
    fs::remove_all(root / fs::u8path(u8"元数据"));
    assert(engine.check(root.u8string(), {disabled}).empty());

    fs::remove_all(root);
    std::cout << "RuleCheckEngine tests passed\n";
    return 0;
}
