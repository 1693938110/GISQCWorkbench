#include "RuleCheckEngine.h"

#include "DatasetMetadataReader.h"
#include "DatasetScanner.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <limits>
#include <regex>
#include <set>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef GISQC_HAVE_GDAL
#include <cpl_conv.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#endif

namespace gisqc {

namespace fs = std::filesystem;

namespace {

fs::path pathFromText(const std::string& path) {
#ifdef _WIN32
    if (path.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return fs::path(path);
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), size);
    wide.resize(static_cast<std::size_t>(size - 1));
    return fs::path(wide);
#else
    return fs::u8path(path);
#endif
}

std::string pathText(const fs::path& path) {
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

#ifdef GISQC_HAVE_GDAL
double parameterDouble(const RuleDefinition& rule, const std::string& name, double defaultValue) {
    const auto it = rule.parameters.find(name);
    if (it == rule.parameters.end()) {
        return defaultValue;
    }
    try {
        return std::stod(it->second);
    } catch (...) {
        return defaultValue;
    }
}

int parameterInt(const RuleDefinition& rule, const std::string& name, int defaultValue) {
    const auto it = rule.parameters.find(name);
    if (it == rule.parameters.end()) {
        return defaultValue;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return defaultValue;
    }
}

bool tryParameterDouble(const RuleDefinition& rule, const std::string& name, double& value) {
    const auto it = rule.parameters.find(name);
    if (it == rule.parameters.end()) {
        return false;
    }
    try {
        value = std::stod(it->second);
        return true;
    } catch (...) {
        return false;
    }
}

bool tryWorkingExtent(const RuleDefinition& rule, OGREnvelope& extent) {
    const auto bboxIt = rule.parameters.find("bbox");
    if (bboxIt != rule.parameters.end()) {
        std::stringstream stream(bboxIt->second);
        std::string item;
        std::vector<double> values;
        while (std::getline(stream, item, ',')) {
            try {
                values.push_back(std::stod(item));
            } catch (...) {
                return false;
            }
        }
        if (values.size() == 4) {
            extent.MinX = values[0];
            extent.MinY = values[1];
            extent.MaxX = values[2];
            extent.MaxY = values[3];
            return extent.MinX <= extent.MaxX && extent.MinY <= extent.MaxY;
        }
    }

    return tryParameterDouble(rule, "minX", extent.MinX) &&
           tryParameterDouble(rule, "minY", extent.MinY) &&
           tryParameterDouble(rule, "maxX", extent.MaxX) &&
           tryParameterDouble(rule, "maxY", extent.MaxY) &&
           extent.MinX <= extent.MaxX && extent.MinY <= extent.MaxY;
}

bool envelopeWithin(const OGREnvelope& candidate, const OGREnvelope& allowed, double tolerance) {
    return candidate.MinX >= allowed.MinX - tolerance &&
           candidate.MinY >= allowed.MinY - tolerance &&
           candidate.MaxX <= allowed.MaxX + tolerance &&
           candidate.MaxY <= allowed.MaxY + tolerance;
}

std::string envelopeText(const OGREnvelope& envelope) {
    std::ostringstream out;
    out << envelope.MinX << "," << envelope.MinY << "," << envelope.MaxX << "," << envelope.MaxY;
    return out.str();
}

fs::path executableDirectory() {
#ifdef _WIN32
    wchar_t buffer[MAX_PATH]{};
    const DWORD size = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (size > 0 && size < MAX_PATH) {
        return fs::path(buffer).parent_path();
    }
#endif
    return fs::current_path();
}

void configureGdalRuntime() {
    static bool configured = false;
    if (configured) {
        return;
    }
    configured = true;

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

int ringPointCount(const OGRLineString* ring) {
    return ring ? ring->getNumPoints() : 0;
}

int geometryNodeCount(const OGRGeometry* geometry) {
    if (!geometry) {
        return 0;
    }
    if (const auto* line = dynamic_cast<const OGRLineString*>(geometry)) {
        return ringPointCount(line);
    }
    if (const auto* polygon = dynamic_cast<const OGRPolygon*>(geometry)) {
        int count = ringPointCount(polygon->getExteriorRing());
        for (int i = 0; i < polygon->getNumInteriorRings(); ++i) {
            count += ringPointCount(polygon->getInteriorRing(i));
        }
        return count;
    }
    if (const auto* collection = dynamic_cast<const OGRGeometryCollection*>(geometry)) {
        int count = 0;
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            count += geometryNodeCount(collection->getGeometryRef(i));
        }
        return count;
    }
    return 0;
}

double segmentLength2d(const OGRLineString* line, int a, int b) {
    const double dx = line->getX(a) - line->getX(b);
    const double dy = line->getY(a) - line->getY(b);
    return std::sqrt(dx * dx + dy * dy);
}

double minSegmentLength(const OGRLineString* line) {
    if (!line || line->getNumPoints() < 2) {
        return std::numeric_limits<double>::infinity();
    }
    double best = std::numeric_limits<double>::infinity();
    for (int i = 1; i < line->getNumPoints(); ++i) {
        best = std::min(best, segmentLength2d(line, i - 1, i));
    }
    return best;
}

double minPolygonSegmentLength(const OGRGeometry* geometry) {
    if (!geometry) {
        return std::numeric_limits<double>::infinity();
    }
    if (const auto* polygon = dynamic_cast<const OGRPolygon*>(geometry)) {
        double best = minSegmentLength(polygon->getExteriorRing());
        for (int i = 0; i < polygon->getNumInteriorRings(); ++i) {
            best = std::min(best, minSegmentLength(polygon->getInteriorRing(i)));
        }
        return best;
    }
    if (const auto* collection = dynamic_cast<const OGRGeometryCollection*>(geometry)) {
        double best = std::numeric_limits<double>::infinity();
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            best = std::min(best, minPolygonSegmentLength(collection->getGeometryRef(i)));
        }
        return best;
    }
    return std::numeric_limits<double>::infinity();
}

bool ringHasSharpAngle(const OGRLineString* ring, double thresholdDegrees) {
    if (!ring || ring->getNumPoints() < 4 || thresholdDegrees <= 0.0) {
        return false;
    }
    const int pointCount = ring->getNumPoints();
    const int uniquePointCount = (ring->getX(0) == ring->getX(pointCount - 1) && ring->getY(0) == ring->getY(pointCount - 1))
        ? pointCount - 1
        : pointCount;
    if (uniquePointCount < 3) {
        return false;
    }

    constexpr double pi = 3.14159265358979323846;
    for (int i = 0; i < uniquePointCount; ++i) {
        const int prev = (i - 1 + uniquePointCount) % uniquePointCount;
        const int next = (i + 1) % uniquePointCount;
        const double ax = ring->getX(prev) - ring->getX(i);
        const double ay = ring->getY(prev) - ring->getY(i);
        const double bx = ring->getX(next) - ring->getX(i);
        const double by = ring->getY(next) - ring->getY(i);
        const double la = std::sqrt(ax * ax + ay * ay);
        const double lb = std::sqrt(bx * bx + by * by);
        if (la <= 0.0 || lb <= 0.0) {
            continue;
        }
        double cosine = (ax * bx + ay * by) / (la * lb);
        cosine = std::max(-1.0, std::min(1.0, cosine));
        const double angle = std::acos(cosine) * 180.0 / pi;
        if (angle < thresholdDegrees) {
            return true;
        }
    }
    return false;
}

bool geometryHasSharpAngle(const OGRGeometry* geometry, double thresholdDegrees) {
    if (!geometry) {
        return false;
    }
    if (const auto* polygon = dynamic_cast<const OGRPolygon*>(geometry)) {
        if (ringHasSharpAngle(polygon->getExteriorRing(), thresholdDegrees)) {
            return true;
        }
        for (int i = 0; i < polygon->getNumInteriorRings(); ++i) {
            if (ringHasSharpAngle(polygon->getInteriorRing(i), thresholdDegrees)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* collection = dynamic_cast<const OGRGeometryCollection*>(geometry)) {
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            if (geometryHasSharpAngle(collection->getGeometryRef(i), thresholdDegrees)) {
                return true;
            }
        }
    }
    return false;
}

int interiorRingCount(const OGRGeometry* geometry) {
    if (!geometry) {
        return 0;
    }
    if (const auto* polygon = dynamic_cast<const OGRPolygon*>(geometry)) {
        return polygon->getNumInteriorRings();
    }
    if (const auto* collection = dynamic_cast<const OGRGeometryCollection*>(geometry)) {
        int count = 0;
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            count += interiorRingCount(collection->getGeometryRef(i));
        }
        return count;
    }
    return 0;
}

bool isCurvedGeometryType(OGRwkbGeometryType type) {
    switch (wkbFlatten(type)) {
    case wkbCircularString:
    case wkbCompoundCurve:
    case wkbCurvePolygon:
    case wkbMultiCurve:
    case wkbMultiSurface:
        return true;
    default:
        return false;
    }
}

struct GeometrySnapshot {
    std::string fid;
    std::string sourcePath;
    std::unique_ptr<OGRGeometry> geometry;
};

double geometryLinearLength(const OGRGeometry* geometry) {
    if (!geometry) {
        return 0.0;
    }
    if (const auto* curve = dynamic_cast<const OGRCurve*>(geometry)) {
        return curve->get_Length();
    }
    if (const auto* multiCurve = dynamic_cast<const OGRMultiCurve*>(geometry)) {
        return multiCurve->get_Length();
    }
    if (const auto* collection = dynamic_cast<const OGRGeometryCollection*>(geometry)) {
        double length = 0.0;
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            length += geometryLinearLength(collection->getGeometryRef(i));
        }
        return length;
    }
    return 0.0;
}

double geometrySurfaceArea(const OGRGeometry* geometry) {
    if (!geometry) {
        return 0.0;
    }
    if (const auto* surface = dynamic_cast<const OGRSurface*>(geometry)) {
        return surface->get_Area();
    }
    if (const auto* multiSurface = dynamic_cast<const OGRMultiSurface*>(geometry)) {
        return multiSurface->get_Area();
    }
    if (const auto* collection = dynamic_cast<const OGRGeometryCollection*>(geometry)) {
        double area = 0.0;
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            area += geometrySurfaceArea(collection->getGeometryRef(i));
        }
        return area;
    }
    return 0.0;
}

bool isLineLike(OGRwkbGeometryType type) {
    const auto flat = wkbFlatten(type);
    return flat == wkbLineString || flat == wkbMultiLineString || flat == wkbCircularString ||
           flat == wkbCompoundCurve || flat == wkbMultiCurve;
}

bool isPolygonLike(OGRwkbGeometryType type) {
    const auto flat = wkbFlatten(type);
    return flat == wkbPolygon || flat == wkbMultiPolygon || flat == wkbCurvePolygon || flat == wkbMultiSurface;
}

bool nearlySamePoint(const OGRPoint& left, const OGRPoint& right, double tolerance) {
    const double dx = left.getX() - right.getX();
    const double dy = left.getY() - right.getY();
    return std::sqrt(dx * dx + dy * dy) <= tolerance;
}

void collectLineEndpoints(const OGRGeometry* geometry, std::vector<OGRPoint>& endpoints) {
    if (!geometry) {
        return;
    }
    if (const auto* line = dynamic_cast<const OGRLineString*>(geometry)) {
        if (line->getNumPoints() >= 2) {
            OGRPoint start;
            OGRPoint end;
            line->getPoint(0, &start);
            line->getPoint(line->getNumPoints() - 1, &end);
            endpoints.push_back(start);
            endpoints.push_back(end);
        }
        return;
    }
    if (const auto* collection = dynamic_cast<const OGRGeometryCollection*>(geometry)) {
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            collectLineEndpoints(collection->getGeometryRef(i), endpoints);
        }
    }
}

bool endpointTouchesAnotherLine(const OGRPoint& endpoint,
                               const std::vector<GeometrySnapshot>& candidates,
                               std::size_t selfIndex,
                               double tolerance) {
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i == selfIndex || !candidates[i].geometry) {
            continue;
        }
        std::vector<OGRPoint> otherEndpoints;
        collectLineEndpoints(candidates[i].geometry.get(), otherEndpoints);
        for (const auto& otherEndpoint : otherEndpoints) {
            if (nearlySamePoint(endpoint, otherEndpoint, tolerance)) {
                return true;
            }
        }
    }
    return false;
}

bool pointCoveredByGeometry(const OGRPoint& point, const OGRGeometry* geometry, double tolerance) {
    if (!geometry) {
        return false;
    }
    if (geometry->Distance(&point) <= tolerance) {
        return true;
    }
    return false;
}

bool lineEndpointsCoveredByGeometry(const OGRGeometry* sourceLine, const OGRGeometry* referenceGeometry, double tolerance) {
    std::vector<OGRPoint> endpoints;
    collectLineEndpoints(sourceLine, endpoints);
    if (endpoints.empty()) {
        return false;
    }
    return std::all_of(endpoints.begin(), endpoints.end(), [&](const auto& endpoint) {
        return pointCoveredByGeometry(endpoint, referenceGeometry, tolerance);
    });
}

bool geometryCoveredByGeometry(const OGRGeometry* source, const OGRGeometry* reference, double tolerance) {
    if (!source || !reference) {
        return false;
    }
    if (reference->Contains(source) || source->Within(reference) || reference->Equals(source)) {
        return true;
    }
    auto* difference = source->Difference(reference);
    if (!difference) {
        return false;
    }
    const bool covered = difference->IsEmpty();
    OGRGeometryFactory::destroyGeometry(difference);
    return covered;
}

bool geometryBoundaryCoveredByGeometry(const OGRGeometry* source, const OGRGeometry* reference, double tolerance) {
    if (!source || !reference) {
        return false;
    }
    auto* boundary = source->Boundary();
    const bool covered = geometryCoveredByGeometry(boundary, reference, tolerance);
    if (boundary) {
        OGRGeometryFactory::destroyGeometry(boundary);
    }
    return covered;
}

bool lineOrPolygonOverlapExists(const OGRGeometry* source, const OGRGeometry* reference, double tolerance) {
    if (!source || !reference) {
        return false;
    }
    auto* intersection = source->Intersection(reference);
    if (!intersection) {
        return false;
    }
    const bool overlap = !intersection->IsEmpty() &&
        (wkbFlatten(intersection->getGeometryType()) == wkbLineString ||
         wkbFlatten(intersection->getGeometryType()) == wkbMultiLineString ||
         wkbFlatten(intersection->getGeometryType()) == wkbPolygon ||
         wkbFlatten(intersection->getGeometryType()) == wkbMultiPolygon);
    OGRGeometryFactory::destroyGeometry(intersection);
    return overlap;
}

bool lineStringHasPseudoNode(const OGRLineString* line, double tolerance) {
    if (!line || line->getNumPoints() < 3) {
        return false;
    }
    for (int i = 1; i + 1 < line->getNumPoints(); ++i) {
        const double ax = line->getX(i) - line->getX(i - 1);
        const double ay = line->getY(i) - line->getY(i - 1);
        const double bx = line->getX(i + 1) - line->getX(i);
        const double by = line->getY(i + 1) - line->getY(i);
        const double lenA = std::sqrt(ax * ax + ay * ay);
        const double lenB = std::sqrt(bx * bx + by * by);
        if (lenA <= tolerance || lenB <= tolerance) {
            continue;
        }
        const double cross = std::abs(ax * by - ay * bx);
        const double dot = ax * bx + ay * by;
        if (cross <= tolerance * (lenA + lenB) && dot > 0.0) {
            return true;
        }
    }
    return false;
}

bool geometryHasPseudoNode(const OGRGeometry* geometry, double tolerance) {
    if (!geometry) {
        return false;
    }
    if (const auto* line = dynamic_cast<const OGRLineString*>(geometry)) {
        return lineStringHasPseudoNode(line, tolerance);
    }
    if (const auto* polygon = dynamic_cast<const OGRPolygon*>(geometry)) {
        if (lineStringHasPseudoNode(polygon->getExteriorRing(), tolerance)) {
            return true;
        }
        for (int i = 0; i < polygon->getNumInteriorRings(); ++i) {
            if (lineStringHasPseudoNode(polygon->getInteriorRing(i), tolerance)) {
                return true;
            }
        }
        return false;
    }
    if (const auto* collection = dynamic_cast<const OGRGeometryCollection*>(geometry)) {
        for (int i = 0; i < collection->getNumGeometries(); ++i) {
            if (geometryHasPseudoNode(collection->getGeometryRef(i), tolerance)) {
                return true;
            }
        }
    }
    return false;
}
#endif

} // namespace

std::vector<IssueRecord> RuleCheckEngine::check(const std::string& rootPath, const std::vector<RuleDefinition>& rules) const {
    std::vector<IssueRecord> issues;
    int issueIndex = 1;
    const fs::path root(pathFromText(rootPath));

    for (const auto& rule : rules) {
        if (!rule.enabled) {
            continue;
        }

        if (rule.code == "A010101") {
            if (!fs::exists(root) || !fs::is_directory(root)) {
                issues.push_back(makeIssue(issueIndex++, rule, "成果目录", pathText(root), "目录无法正常读取：" + pathText(root)));
                continue;
            }
            const auto readIntParameter = [](const RuleDefinition& r, const std::string& name, int defaultValue) {
                const auto it = r.parameters.find(name);
                if (it == r.parameters.end()) {
                    return defaultValue;
                }
                try {
                    return std::stoi(it->second);
                } catch (...) {
                    return defaultValue;
                }
            };
            const int minLevels = readIntParameter(rule, "minLevelCount", 0);
            const int maxLevels = readIntParameter(rule, "maxLevelCount", 0);
            if (minLevels > 0 || maxLevels > 0) {
                int childDirectoryCount = 0;
                for (const auto& entry : fs::directory_iterator(root)) {
                    if (entry.is_directory()) {
                        ++childDirectoryCount;
                    }
                }
                if ((minLevels > 0 && childDirectoryCount < minLevels) ||
                    (maxLevels > 0 && childDirectoryCount > maxLevels)) {
                    issues.push_back(makeIssue(issueIndex++, rule, "成果目录", "层级数量",
                        "目录层级数量不符合要求，当前一级子目录数量=" + std::to_string(childDirectoryCount)));
                }
            }
        } else if (rule.code == "A010102") {
            const auto pattern = rule.parameters.count("rootNameRegex") ? rule.parameters.at("rootNameRegex") : "^[A-Za-z0-9_\\-.]+$";
            const std::regex regex(pattern);
            const auto rootName = pathText(root.filename());
            if (!std::regex_match(rootName, regex)) {
                issues.push_back(makeIssue(issueIndex++, rule, "成果目录", rootName,
                    "目录名称不符合命名要求：" + rootName));
            }
        } else if (rule.code == "A010103") {
            for (const auto& folder : csvParameter(rule, "requiredFolders", "空间数据,文档资料,元数据")) {
                const auto candidate = root / fs::u8path(folder);
                if (!fs::exists(candidate) || !fs::is_directory(candidate)) {
                    issues.push_back(makeIssue(issueIndex++, rule, "成果目录", folder,
                        rule.message.empty() ? "缺少必交付子目录：" + folder : rule.message + "：" + folder));
                }
            }
            for (const auto& relativePath : csvParameter(rule, "requiredPaths")) {
                const auto candidate = root / pathFromText(relativePath);
                if (!isRegularOrDirectory(candidate)) {
                    issues.push_back(makeIssue(issueIndex++, rule, "成果目录", relativePath,
                        "缺失必选目录或文件：" + relativePath));
                }
            }
        } else if (rule.code == "A010104") {
            for (const auto& file : csvParameter(rule, "requiredFiles")) {
                const auto candidate = root / pathFromText(file);
                if (!fs::exists(candidate) || !fs::is_regular_file(candidate)) {
                    issues.push_back(makeIssue(issueIndex++, rule, "成果目录", file,
                        "缺少必交付文件：" + file));
                }
            }
        } else if (rule.code == "A010201") {
            if (!hasSupportedDataset(root)) {
                issues.push_back(makeIssue(issueIndex++, rule, "成果目录", "数据源",
                    "未发现 FileGDB、Shapefile 或 GeoPackage 数据入口"));
            }
        } else if (rule.code == "A010301" && truthyParameter(rule, "scanEmptyFolders", true)) {
            if (fs::exists(root)) {
                for (const auto& entry : fs::recursive_directory_iterator(root)) {
                    if (entry.is_directory() && entry.path().extension() != ".gdb" && isDirectoryEmpty(entry.path())) {
                        issues.push_back(makeIssue(issueIndex++, rule, "成果目录", pathText(entry.path().filename()),
                            "发现空目录：" + pathText(entry.path())));
                    }
                }
            }
        } else if (rule.code == "A010302") {
            const auto pattern = rule.parameters.count("fileNameRegex") ? rule.parameters.at("fileNameRegex") : "^[A-Za-z0-9_\\-.]+$";
            const std::regex regex(pattern);
            if (fs::exists(root)) {
                for (const auto& entry : fs::recursive_directory_iterator(root)) {
                    if (entry.is_regular_file()) {
                        const auto name = pathText(entry.path().filename());
                        if (!std::regex_match(name, regex)) {
                            issues.push_back(makeIssue(issueIndex++, rule, "成果目录", name, "文件命名不符合规范：" + name));
                        }
                    }
                }
            }
        } else if (rule.code == "A010303" && truthyParameter(rule, "checkShapefileSidecars", true)) {
            if (fs::exists(root)) {
                for (const auto& entry : fs::recursive_directory_iterator(root)) {
                    if (!entry.is_regular_file() || entry.path().extension() != ".shp") {
                        continue;
                    }
                    for (const auto& ext : {".shx", ".dbf"}) {
                        auto sidecar = entry.path();
                        sidecar.replace_extension(ext);
                        if (!fs::exists(sidecar)) {
                            issues.push_back(makeIssue(issueIndex++, rule, pathText(entry.path().stem()), pathText(sidecar.filename()),
                                "Shapefile 缺少配套文件：" + pathText(sidecar.filename())));
                        }
                    }
                }
            }
        } else if (rule.code == "A020101") {
            if (fs::exists(root)) {
                for (const auto& entry : fs::recursive_directory_iterator(root)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    std::ifstream in(entry.path(), std::ios::binary);
                    if (!in.good()) {
                        const auto name = pathText(entry.path().filename());
                        issues.push_back(makeIssue(issueIndex++, rule, "成果文件", name, "文件无法正常读取：" + name));
                    }
                }
            }
        } else if (rule.code == "A020102") {
            const auto pattern = rule.parameters.count("fileNameRegex") ? rule.parameters.at("fileNameRegex") : "^[A-Za-z0-9_.-]+$";
            const std::regex regex(pattern);
            if (fs::exists(root)) {
                for (const auto& entry : fs::recursive_directory_iterator(root)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    const auto name = pathText(entry.path().filename());
                    if (!std::regex_match(name, regex)) {
                        issues.push_back(makeIssue(issueIndex++, rule, "成果文件", name, "文件名不符合命名规则：" + name));
                    }
                }
            }
        } else if (rule.code == "A020103") {
            const auto allowedExtensions = csvParameter(rule, "allowedExtensions", ".shp,.shx,.dbf,.prj,.gpkg,.xml,.txt,.md,.csv,.xlsx,.docx,.pdf");
            if (fs::exists(root)) {
                for (const auto& entry : fs::recursive_directory_iterator(root)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    const auto ext = lower(pathText(entry.path().extension()));
                    if (!containsName(allowedExtensions, ext)) {
                        const auto name = pathText(entry.path().filename());
                        issues.push_back(makeIssue(issueIndex++, rule, "成果文件", name, "文件扩展名不符合要求：" + ext));
                    }
                }
            }
        } else if (rule.code == "A020104") {
            const auto requiredText = rule.parameters.count("requiredVersionText") ? rule.parameters.at("requiredVersionText") : "";
            const auto files = csvParameter(rule, "versionFiles");
            if (!requiredText.empty()) {
                for (const auto& file : files) {
                    const auto candidate = root / pathFromText(file);
                    std::ifstream in(candidate, std::ios::binary);
                    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                    if (!in.good() || content.find(requiredText) == std::string::npos) {
                        issues.push_back(makeIssue(issueIndex++, rule, "成果文件", file, "文件版本不符合要求：" + file));
                    }
                }
            }
        } else if (rule.code == "B010201" || rule.code == "B010101" || rule.code == "B020101" || rule.code == "B010102" || rule.code == "B010202" ||
                   rule.code == "B010203" || rule.code == "B010204") {
            const auto names = availableLayerNames(root);
            if (rule.code == "B010102" || rule.code == "B010202") {
                for (const auto& table : csvParameter(rule, "requiredTables")) {
                    if (!containsName(names, table)) {
                        issues.push_back(makeIssue(issueIndex++, rule, "数据库", table, "缺失必选数据集/表：" + table));
                    }
                }
            } else if (rule.code == "B010203") {
                const auto aliasTypes = assignmentParameter(rule, "tableAliasTypes");
                for (const auto& name : names) {
                    const auto it = aliasTypes.find(name);
                    if (it == aliasTypes.end() || it->second.empty()) {
                        issues.push_back(makeIssue(issueIndex++, rule, "数据库", name, "表别名或表类型不符合数据库分层规定：" + name));
                    }
                }
            } else if (rule.code == "B010204") {
                const auto tableDatasets = assignmentParameter(rule, "tableDatasets");
                for (const auto& name : names) {
                    const auto it = tableDatasets.find(name);
                    if (it == tableDatasets.end() || it->second.empty()) {
                        issues.push_back(makeIssue(issueIndex++, rule, "数据库", name, "表与数据集组织结构不符合数据库分层规定：" + name));
                    }
                }
            } else {
                const auto allowed = csvParameter(rule, rule.code == "B020101" ? "allowedTables" : "allowedDatasets");
                if (!allowed.empty()) {
                    for (const auto& name : names) {
                        if (!containsName(allowed, name)) {
                            const auto message = rule.code == "B020101" ? "数据表未匹配到数据表规范：" : "数据集未匹配到数据库分层信息：";
                            issues.push_back(makeIssue(issueIndex++, rule, "数据库", name, message + name));
                        }
                    }
                }
            }
        } else if (rule.code == "B020102" || rule.code == "B020103" || rule.code == "B020104") {
            appendTableStructureIssues(root, rule, issues, issueIndex);
        } else if (rule.code == "B020106" || rule.code == "B020107" || rule.code == "B020108" ||
                   rule.code == "B020201" || rule.code == "B020202") {
            appendAttributeValueIssues(root, rule, issues, issueIndex);
        } else if (rule.code == "C010101") {
            appendGdalGeometryIssues(root, rule, issues, issueIndex);
        } else if (rule.code == "C010201") {
            appendCoordinateIssues(root, rule, issues, issueIndex);
        } else if (rule.code == "C020201" || rule.code == "C020301" || rule.code == "C020302" ||
                   rule.code == "C020303" || rule.code == "C020401" || rule.code == "C020402" ||
                   rule.code == "C020501" || rule.code == "C030101" || rule.code == "C030102" ||
                   rule.code == "C030201" || rule.code == "C030202" || rule.code == "C030203" || rule.code == "C030204" ||
                   rule.code == "C030205" || rule.code == "C030206" ||
                   rule.code == "C030207" || rule.code == "C030208" || rule.code == "C030301" || rule.code == "C030302" ||
                   rule.code == "C030303" || rule.code == "C030304" || rule.code == "C030305" || rule.code == "C030306") {
            appendGdalGeometryIssues(root, rule, issues, issueIndex);
        } else if (rule.code == "C030103" || rule.code == "C030104" || rule.code == "C030105" ||
                   rule.code == "C030106" || rule.code == "C030107" ||
                   rule.code == "C030210" || rule.code == "C030211" || rule.code == "C030212" ||
                   rule.code == "C030213" || rule.code == "C030215" || rule.code == "C030216" ||
                   rule.code == "C030307" || rule.code == "C030308" || rule.code == "C030309" ||
                   rule.code == "C030310" || rule.code == "C030312" || rule.code == "C030313" ||
                   rule.code == "C030314") {
            appendInterLayerTopologyIssues(root, rule, issues, issueIndex);
        }
    }

    return issues;
}

IssueRecord RuleCheckEngine::makeIssue(int issueIndex, const RuleDefinition& rule, const std::string& layerName, const std::string& featureId, const std::string& description) {
    IssueRecord issue;
    std::ostringstream id;
    id << "P-";
    id.width(4);
    id.fill('0');
    id << issueIndex;
    issue.issueId = id.str();
    issue.ruleCode = rule.code;
    issue.layerName = layerName;
    issue.featureId = featureId;
    issue.description = description;
    issue.severity = severityFromString(rule.severity);
    issue.status = "待处理";
    return issue;
}

IssueRecord RuleCheckEngine::makeGeometryIssue(int issueIndex,
                                               const RuleDefinition& rule,
                                               const std::string& layerName,
                                               const std::string& featureId,
                                               const std::string& description,
                                               const std::string& sourcePath,
                                               const void* geometry) {
    auto issue = makeIssue(issueIndex, rule, layerName, featureId, description);
    issue.sourcePath = sourcePath;
#ifdef GISQC_HAVE_GDAL
    const auto* ogrGeometry = static_cast<const OGRGeometry*>(geometry);
    if (ogrGeometry) {
        char* wkt = nullptr;
        if (ogrGeometry->exportToWkt(&wkt) == OGRERR_NONE && wkt) {
            issue.geometryWkt = wkt;
            CPLFree(wkt);
        }
        OGREnvelope envelope{};
        ogrGeometry->getEnvelope(&envelope);
        issue.previewX = (envelope.MinX + envelope.MaxX) / 2.0;
        issue.previewY = (envelope.MinY + envelope.MaxY) / 2.0;
        issue.hasPreviewGeometry = !issue.geometryWkt.empty();
    }
#else
    (void)geometry;
#endif
    return issue;
}

Severity RuleCheckEngine::severityFromString(const std::string& severity) {
    std::string lower = severity;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "warning" || lower == "warn" || severity == "\xe8\xad\xa6\xe5\x91\x8a") {
        return Severity::Warning;
    }
    if (lower == "error" || severity == "\xe9\x94\x99\xe8\xaf\xaf") {
        return Severity::Error;
    }
    if (severity == "\xe6\x8f\x90\xe7\xa4\xba") {
        return Severity::Info;
    }
    return Severity::Info;
}

bool RuleCheckEngine::truthyParameter(const RuleDefinition& rule, const std::string& name, bool defaultValue) {
    const auto it = rule.parameters.find(name);
    if (it == rule.parameters.end()) {
        return defaultValue;
    }
    std::string lower = it->second;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "true" || lower == "1" || lower == "yes" || lower == "是";
}

std::vector<std::string> RuleCheckEngine::csvParameter(const RuleDefinition& rule, const std::string& name, const std::string& defaultValue) {
    const auto it = rule.parameters.find(name);
    const std::string csv = it == rule.parameters.end() ? defaultValue : it->second;

    std::vector<std::string> values;
    std::stringstream stream(csv);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            values.push_back(item);
        }
    }
    return values;
}

std::vector<std::string> RuleCheckEngine::semicolonParameter(const RuleDefinition& rule, const std::string& name) {
    const auto it = rule.parameters.find(name);
    if (it == rule.parameters.end()) {
        return {};
    }
    std::vector<std::string> values;
    std::stringstream stream(it->second);
    std::string item;
    while (std::getline(stream, item, ';')) {
        item = trim(item);
        if (!item.empty()) {
            values.push_back(item);
        }
    }
    return values;
}

std::map<std::string, std::string> RuleCheckEngine::assignmentParameter(const RuleDefinition& rule, const std::string& name) {
    std::map<std::string, std::string> assignments;
    for (const auto& item : semicolonParameter(rule, name)) {
        const auto pos = item.find('=');
        if (pos == std::string::npos || pos == 0) {
            continue;
        }
        const auto key = trim(item.substr(0, pos));
        const auto value = trim(item.substr(pos + 1));
        if (!key.empty()) {
            assignments[key] = value;
        }
    }
    return assignments;
}

bool RuleCheckEngine::hasSupportedDataset(const fs::path& rootPath) {
    if (!fs::exists(rootPath)) {
        return false;
    }
    for (const auto& entry : fs::recursive_directory_iterator(rootPath)) {
        if (isSupportedDatasetEntry(entry)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> RuleCheckEngine::availableLayerNames(const fs::path& rootPath) {
    std::set<std::string> names;
    DatasetScanner scanner;
    const auto scanned = scanner.scan(pathText(rootPath));
    for (const auto& source : scanned.sources) {
        if (!source.layerName.empty()) {
            names.insert(source.layerName);
        }

#ifdef GISQC_HAVE_GDAL
        if (source.type == DatasetType::FileGDB || source.type == DatasetType::GeoPackage) {
            configureGdalRuntime();
            GDALAllRegister();
            GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(source.path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
            if (!dataset) {
                continue;
            }
            const int layerCount = dataset->GetLayerCount();
            for (int i = 0; i < layerCount; ++i) {
                OGRLayer* layer = dataset->GetLayer(i);
                if (layer && layer->GetName()) {
                    names.insert(layer->GetName());
                }
            }
            GDALClose(dataset);
        }
#endif
    }
    return {names.begin(), names.end()};
}

std::vector<RuleCheckEngine::FieldInfo> RuleCheckEngine::availableFields(const fs::path& rootPath) {
    std::vector<FieldInfo> fields;
    DatasetScanner scanner;
    const auto scanned = scanner.scan(pathText(rootPath));
    for (const auto& source : scanned.sources) {
        if (source.type == DatasetType::Shapefile) {
            auto dbfFields = readDbfFields(source.path, source.layerName);
            fields.insert(fields.end(), dbfFields.begin(), dbfFields.end());
        }

#ifdef GISQC_HAVE_GDAL
        if (source.type == DatasetType::FileGDB || source.type == DatasetType::GeoPackage) {
            configureGdalRuntime();
            GDALAllRegister();
            GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(source.path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
            if (!dataset) {
                continue;
            }
            const int layerCount = dataset->GetLayerCount();
            for (int i = 0; i < layerCount; ++i) {
                OGRLayer* layer = dataset->GetLayer(i);
                if (!layer) {
                    continue;
                }
                const std::string tableName = layer->GetName() ? layer->GetName() : "";
                OGRFeatureDefn* defn = layer->GetLayerDefn();
                if (!defn) {
                    continue;
                }
                const int fieldCount = defn->GetFieldCount();
                for (int f = 0; f < fieldCount; ++f) {
                    const OGRFieldDefn* fieldDefn = defn->GetFieldDefn(f);
                    if (!fieldDefn) {
                        continue;
                    }
                    FieldInfo info;
                    info.tableName = tableName;
                    info.fieldName = fieldDefn->GetNameRef() ? fieldDefn->GetNameRef() : "";
                    info.type = OGRFieldDefn::GetFieldTypeName(fieldDefn->GetType());
                    info.width = fieldDefn->GetWidth();
                    fields.push_back(std::move(info));
                }
            }
            GDALClose(dataset);
        }
#endif
    }
    return fields;
}

std::vector<RuleCheckEngine::FieldInfo> RuleCheckEngine::readDbfFields(const std::string& shpPath, const std::string& tableName) {
    fs::path dbf = pathFromText(shpPath);
    dbf.replace_extension(".dbf");
    std::ifstream in(dbf, std::ios::binary);
    if (!in) {
        return {};
    }

    unsigned char header[32]{};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(header))) {
        return {};
    }
    const int headerLength = static_cast<int>(header[8]) | (static_cast<int>(header[9]) << 8);
    if (headerLength < 33) {
        return {};
    }
    const int fieldCount = (headerLength - 33) / 32;
    std::vector<FieldInfo> fields;
    for (int i = 0; i < fieldCount; ++i) {
        unsigned char descriptor[32]{};
        in.read(reinterpret_cast<char*>(descriptor), sizeof(descriptor));
        if (in.gcount() != static_cast<std::streamsize>(sizeof(descriptor)) || descriptor[0] == 0x0D) {
            break;
        }
        std::string name(reinterpret_cast<char*>(descriptor), reinterpret_cast<char*>(descriptor) + 11);
        name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
        name = trim(name);
        if (name.empty()) {
            continue;
        }

        FieldInfo info;
        info.tableName = tableName;
        info.fieldName = name;
        switch (static_cast<char>(descriptor[11])) {
        case 'C':
            info.type = "text";
            break;
        case 'N':
        case 'F':
        case 'B':
            info.type = descriptor[17] > 0 ? "float" : "int";
            break;
        case 'D':
        case 'T':
            info.type = "date";
            break;
        case 'L':
            info.type = "bool";
            break;
        default:
            info.type = "other";
            break;
        }
        info.width = static_cast<int>(descriptor[16]);
        fields.push_back(std::move(info));
    }
    return fields;
}

std::vector<RuleCheckEngine::FieldValueRecord> RuleCheckEngine::availableRecords(const fs::path& rootPath) {
    std::vector<FieldValueRecord> records;
    DatasetScanner scanner;
    const auto scanned = scanner.scan(pathText(rootPath));
    for (const auto& source : scanned.sources) {
        if (source.type == DatasetType::Shapefile) {
            auto dbfRecords = readDbfRecords(source.path, source.layerName);
            records.insert(records.end(), dbfRecords.begin(), dbfRecords.end());
        }

#ifdef GISQC_HAVE_GDAL
        if (source.type == DatasetType::FileGDB || source.type == DatasetType::GeoPackage) {
            configureGdalRuntime();
            GDALAllRegister();
            GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(source.path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
            if (!dataset) {
                continue;
            }
            const int layerCount = dataset->GetLayerCount();
            for (int i = 0; i < layerCount; ++i) {
                OGRLayer* layer = dataset->GetLayer(i);
                if (!layer) {
                    continue;
                }
                const std::string tableName = layer->GetName() ? layer->GetName() : source.layerName;
                OGRFeatureDefn* defn = layer->GetLayerDefn();
                if (!defn) {
                    continue;
                }
                layer->ResetReading();
                OGRFeature* feature = nullptr;
                while ((feature = layer->GetNextFeature()) != nullptr) {
                    FieldValueRecord record;
                    record.tableName = tableName;
                    record.featureId = std::to_string(feature->GetFID());
                    const int fieldCount = defn->GetFieldCount();
                    for (int f = 0; f < fieldCount; ++f) {
                        const OGRFieldDefn* fieldDefn = defn->GetFieldDefn(f);
                        if (!fieldDefn || !fieldDefn->GetNameRef()) {
                            continue;
                        }
                        record.values[fieldDefn->GetNameRef()] =
                            feature->IsFieldSetAndNotNull(f) ? feature->GetFieldAsString(f) : "";
                    }
                    records.push_back(std::move(record));
                    OGRFeature::DestroyFeature(feature);
                }
            }
            GDALClose(dataset);
        }
#endif
    }
    return records;
}

std::vector<RuleCheckEngine::FieldValueRecord> RuleCheckEngine::readDbfRecords(const std::string& shpPath, const std::string& tableName) {
    struct DbfField {
        std::string name;
        int offset{0};
        int width{0};
    };

    fs::path dbf = pathFromText(shpPath);
    dbf.replace_extension(".dbf");
    std::ifstream in(dbf, std::ios::binary);
    if (!in) {
        return {};
    }

    unsigned char header[32]{};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(header))) {
        return {};
    }

    const int recordCount = static_cast<int>(header[4]) |
                            (static_cast<int>(header[5]) << 8) |
                            (static_cast<int>(header[6]) << 16) |
                            (static_cast<int>(header[7]) << 24);
    const int headerLength = static_cast<int>(header[8]) | (static_cast<int>(header[9]) << 8);
    const int recordLength = static_cast<int>(header[10]) | (static_cast<int>(header[11]) << 8);
    if (recordCount <= 0 || headerLength < 33 || recordLength <= 1) {
        return {};
    }

    std::vector<DbfField> fields;
    int offset = 1;
    const int fieldCount = (headerLength - 33) / 32;
    for (int i = 0; i < fieldCount; ++i) {
        unsigned char descriptor[32]{};
        in.read(reinterpret_cast<char*>(descriptor), sizeof(descriptor));
        if (in.gcount() != static_cast<std::streamsize>(sizeof(descriptor)) || descriptor[0] == 0x0D) {
            break;
        }
        std::string name(reinterpret_cast<char*>(descriptor), reinterpret_cast<char*>(descriptor) + 11);
        name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
        name = trim(name);
        const int width = static_cast<int>(descriptor[16]);
        if (!name.empty() && width > 0) {
            fields.push_back({name, offset, width});
        }
        offset += width;
    }

    in.seekg(headerLength, std::ios::beg);
    std::vector<FieldValueRecord> records;
    std::vector<char> buffer(static_cast<std::size_t>(recordLength));
    for (int i = 0; i < recordCount; ++i) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (in.gcount() != static_cast<std::streamsize>(buffer.size())) {
            break;
        }
        if (!buffer.empty() && buffer[0] == '*') {
            continue;
        }

        FieldValueRecord record;
        record.tableName = tableName;
        record.featureId = std::to_string(i + 1);
        for (const auto& field : fields) {
            if (field.offset + field.width > recordLength) {
                continue;
            }
            record.values[field.name] = trim(std::string(buffer.data() + field.offset, static_cast<std::size_t>(field.width)));
        }
        records.push_back(std::move(record));
    }
    return records;
}

bool RuleCheckEngine::hasField(const std::vector<FieldInfo>& fields, const std::string& tableName, const std::string& fieldName) {
    return findField(fields, tableName, fieldName) != nullptr;
}

const RuleCheckEngine::FieldInfo* RuleCheckEngine::findField(const std::vector<FieldInfo>& fields, const std::string& tableName, const std::string& fieldName) {
    const auto table = lower(trim(tableName));
    const auto field = lower(trim(fieldName));
    for (const auto& info : fields) {
        if (lower(trim(info.tableName)) == table && lower(trim(info.fieldName)) == field) {
            return &info;
        }
    }
    return nullptr;
}

void RuleCheckEngine::appendTableStructureIssues(const fs::path& rootPath, const RuleDefinition& rule, std::vector<IssueRecord>& issues, int& issueIndex) {
    const auto fields = availableFields(rootPath);

    if (rule.code == "B020102") {
        for (const auto& item : csvParameter(rule, "requiredFields")) {
            const auto dot = item.find('.');
            if (dot == std::string::npos || dot == 0 || dot + 1 >= item.size()) {
                continue;
            }
            const auto tableName = trim(item.substr(0, dot));
            const auto fieldName = trim(item.substr(dot + 1));
            if (!hasField(fields, tableName, fieldName)) {
                issues.push_back(makeIssue(issueIndex++, rule, tableName, fieldName, "缺失字段：" + fieldName));
            }
        }

        const auto allowed = csvParameter(rule, "allowedFields");
        if (!allowed.empty()) {
            for (const auto& info : fields) {
                const std::string key = info.tableName + "." + info.fieldName;
                const bool systemField = containsName({"objectid", "fid", "shape", "shape_length", "shape_area", "geom", "geometry"}, info.fieldName);
                if (!systemField && !containsName(allowed, key)) {
                    issues.push_back(makeIssue(issueIndex++, rule, info.tableName, info.fieldName, "存在冗余字段：" + info.fieldName));
                }
            }
        }
    } else if (rule.code == "B020103") {
        for (const auto& [key, expectedType] : assignmentParameter(rule, "fieldTypes")) {
            const auto dot = key.find('.');
            if (dot == std::string::npos) {
                continue;
            }
            const auto tableName = trim(key.substr(0, dot));
            const auto fieldName = trim(key.substr(dot + 1));
            const auto* field = findField(fields, tableName, fieldName);
            if (!field) {
                continue;
            }
            const auto actual = lower(field->type);
            const auto expected = lower(expectedType);
            const bool matches =
                (expected == "text" && (actual.find("string") != std::string::npos || actual.find("text") != std::string::npos)) ||
                (expected == "int" && (actual.find("integer") != std::string::npos || actual == "int")) ||
                (expected == "float" && (actual.find("real") != std::string::npos || actual.find("float") != std::string::npos || actual.find("double") != std::string::npos)) ||
                (expected == "date" && actual.find("date") != std::string::npos);
            if (!matches) {
                issues.push_back(makeIssue(issueIndex++, rule, tableName, fieldName,
                    "字段类型不规范，应为{" + expectedType + "}，实际为{" + field->type + "}"));
            }
        }
    } else if (rule.code == "B020104") {
        for (const auto& [key, expectedLength] : assignmentParameter(rule, "fieldLengths")) {
            const auto dot = key.find('.');
            if (dot == std::string::npos) {
                continue;
            }
            const auto tableName = trim(key.substr(0, dot));
            const auto fieldName = trim(key.substr(dot + 1));
            const auto* field = findField(fields, tableName, fieldName);
            if (!field) {
                continue;
            }
            int expected = 0;
            try {
                expected = std::stoi(expectedLength);
            } catch (...) {
                continue;
            }
            if (expected > 0 && field->width > 0 && field->width != expected) {
                issues.push_back(makeIssue(issueIndex++, rule, tableName, fieldName,
                    "字段长度不规范，应为{" + expectedLength + "}，实际为{" + std::to_string(field->width) + "}"));
            }
        }
    }
}

void RuleCheckEngine::appendAttributeValueIssues(const fs::path& rootPath, const RuleDefinition& rule, std::vector<IssueRecord>& issues, int& issueIndex) {
    const auto records = availableRecords(rootPath);

    if (rule.code == "B020108") {
        for (const auto& item : csvParameter(rule, "requiredValueFields")) {
            std::string tableName;
            std::string fieldName;
            if (!parseFieldReference(item, tableName, fieldName)) {
                continue;
            }
            for (const auto& record : records) {
                if (!containsName({record.tableName}, tableName)) {
                    continue;
                }
                const auto* value = recordValue(record, fieldName);
                if (!value || trim(*value).empty()) {
                    issues.push_back(makeIssue(issueIndex++, rule, tableName, record.featureId,
                        "必填字段为空：" + fieldName));
                }
            }
        }
    } else if (rule.code == "B020107") {
        for (const auto& item : csvParameter(rule, "uniqueFields")) {
            std::string tableName;
            std::string fieldName;
            if (!parseFieldReference(item, tableName, fieldName)) {
                continue;
            }
            std::map<std::string, std::string> seen;
            for (const auto& record : records) {
                if (!containsName({record.tableName}, tableName)) {
                    continue;
                }
                const auto* value = recordValue(record, fieldName);
                const auto normalized = value ? lower(trim(*value)) : "";
                if (normalized.empty()) {
                    continue;
                }
                const auto inserted = seen.emplace(normalized, record.featureId);
                if (!inserted.second) {
                    issues.push_back(makeIssue(issueIndex++, rule, tableName, record.featureId,
                        "字段值不唯一：" + fieldName + "=" + *value + "，首次出现记录=" + inserted.first->second));
                }
            }
        }
    } else if (rule.code == "B020106" || rule.code == "B020201") {
        const auto parameterName = rule.code == "B020106" ? "valueDomains" : "singleFieldEnums";
        for (const auto& [key, rawAllowedValues] : assignmentParameter(rule, parameterName)) {
            std::string tableName;
            std::string fieldName;
            if (!parseFieldReference(key, tableName, fieldName)) {
                continue;
            }
            auto allowedValues = splitValueList(rawAllowedValues, '|');
            if (allowedValues.size() <= 1) {
                allowedValues = splitValueList(rawAllowedValues, ',');
            }
            if (allowedValues.empty()) {
                continue;
            }

            for (const auto& record : records) {
                if (!containsName({record.tableName}, tableName)) {
                    continue;
                }
                const auto* value = recordValue(record, fieldName);
                if (!value || trim(*value).empty()) {
                    continue;
                }
                if (!valueInList(*value, allowedValues)) {
                    issues.push_back(makeIssue(issueIndex++, rule, tableName, record.featureId,
                        "字段值不在允许范围内：" + fieldName + "=" + *value));
                }
            }
        }
    } else if (rule.code == "B020202") {
        for (const auto& [key, rawAllowedTuples] : assignmentParameter(rule, "multiFieldEnums")) {
            const auto dot = key.find('.');
            if (dot == std::string::npos || dot == 0 || dot + 1 >= key.size()) {
                continue;
            }
            const auto tableName = trim(key.substr(0, dot));
            const auto fieldNames = splitValueList(key.substr(dot + 1), '+');
            const auto allowedTuples = splitValueList(rawAllowedTuples, ',');
            if (tableName.empty() || fieldNames.empty() || allowedTuples.empty()) {
                continue;
            }

            for (const auto& record : records) {
                if (!containsName({record.tableName}, tableName)) {
                    continue;
                }

                std::string tuple;
                bool allEmpty = true;
                bool anyEmpty = false;
                for (std::size_t i = 0; i < fieldNames.size(); ++i) {
                    const auto* value = recordValue(record, fieldNames[i]);
                    const auto normalized = value ? trim(*value) : "";
                    if (!normalized.empty()) {
                        allEmpty = false;
                    } else {
                        anyEmpty = true;
                    }
                    if (i > 0) {
                        tuple += "|";
                    }
                    tuple += normalized;
                }
                if (allEmpty || anyEmpty) {
                    continue;
                }
                if (!valueInList(tuple, allowedTuples)) {
                    issues.push_back(makeIssue(issueIndex++, rule, tableName, record.featureId,
                        "多字段取值组合不在允许关系内：" + tuple));
                }
            }
        }
    }
}

bool RuleCheckEngine::containsName(const std::vector<std::string>& names, const std::string& expected) {
    const auto target = lower(trim(expected));
    return std::any_of(names.begin(), names.end(), [&](const auto& name) {
        return lower(trim(name)) == target;
    });
}

bool RuleCheckEngine::parseFieldReference(const std::string& reference, std::string& tableName, std::string& fieldName) {
    const auto dot = reference.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= reference.size()) {
        return false;
    }
    tableName = trim(reference.substr(0, dot));
    fieldName = trim(reference.substr(dot + 1));
    return !tableName.empty() && !fieldName.empty();
}

std::vector<std::string> RuleCheckEngine::splitValueList(const std::string& value, char delimiter) {
    std::vector<std::string> values;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, delimiter)) {
        item = trim(item);
        if (!item.empty()) {
            values.push_back(item);
        }
    }
    return values;
}

const std::string* RuleCheckEngine::recordValue(const FieldValueRecord& record, const std::string& fieldName) {
    const auto target = lower(trim(fieldName));
    for (const auto& [name, value] : record.values) {
        if (lower(trim(name)) == target) {
            return &value;
        }
    }
    return nullptr;
}

bool RuleCheckEngine::valueInList(const std::string& value, const std::vector<std::string>& allowedValues) {
    const auto normalized = lower(trim(value));
    return std::any_of(allowedValues.begin(), allowedValues.end(), [&](const auto& allowed) {
        return lower(trim(allowed)) == normalized;
    });
}

void RuleCheckEngine::appendCoordinateIssues(const fs::path& rootPath, const RuleDefinition& rule, std::vector<IssueRecord>& issues, int& issueIndex) {
    const auto expectedWkid = rule.parameters.count("wkid") ? rule.parameters.at("wkid") : "";
    const auto expectedName = rule.parameters.count("name") ? lower(rule.parameters.at("name")) : "";
    DatasetScanner scanner;
    DatasetMetadataReader reader;
    const auto scanned = scanner.scan(pathText(rootPath));

    for (const auto& source : scanned.sources) {
        const auto metadata = reader.read(source);
        const auto crs = lower(metadata.crsText);
        bool matches = false;
        if (!expectedWkid.empty() && crs.find(expectedWkid) != std::string::npos) {
            matches = true;
        }
        if (!expectedName.empty() && crs.find(expectedName) != std::string::npos) {
            matches = true;
        }
        if (!matches && crs.find("待接入") == std::string::npos) {
            issues.push_back(makeIssue(issueIndex++, rule, source.layerName, source.displayName,
                "坐标系统不符合要求，实际为{" + metadata.crsText + "}"));
        }
    }
}

void RuleCheckEngine::appendGdalGeometryIssues(const fs::path& rootPath, const RuleDefinition& rule, std::vector<IssueRecord>& issues, int& issueIndex) {
#ifdef GISQC_HAVE_GDAL
    const auto selectedLayers = csvParameter(rule, "layers");
    const double tolerance = parameterDouble(rule, "tolerance", 0.001);
    const double areaTolerance = parameterDouble(rule, "areaTolerance", tolerance);
    const double angleTolerance = parameterDouble(rule, "angleTolerance", 20.0);
    const int minNodes = parameterInt(rule, "minNodes", 2);
    const int maxNodes = parameterInt(rule, "maxNodes", 0);
    const bool needsSameLayerPairs = rule.code == "C030201" || rule.code == "C030202" || rule.code == "C030302" || rule.code == "C030301";
    const bool needsLineEndpointGraph = rule.code == "C030203" || rule.code == "C030205";
    OGREnvelope allowedExtent{};
    const bool hasWorkingExtent = rule.code == "C010101" && tryWorkingExtent(rule, allowedExtent);
    DatasetScanner scanner;
    const auto scanned = scanner.scan(pathText(rootPath));

    configureGdalRuntime();
    GDALAllRegister();
    for (const auto& source : scanned.sources) {
        GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(source.path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
        if (!dataset) {
            continue;
        }

        const int layerCount = dataset->GetLayerCount();
        for (int i = 0; i < layerCount; ++i) {
            OGRLayer* layer = dataset->GetLayer(i);
            if (!layer) {
                continue;
            }
            const std::string layerName = layer->GetName() ? layer->GetName() : source.layerName;
            if (!selectedLayers.empty() && !containsName(selectedLayers, layerName)) {
                continue;
            }

            std::map<std::string, std::string> seenPoints;
            std::vector<GeometrySnapshot> pairCandidates;
            layer->ResetReading();
            OGRFeature* feature = nullptr;
            while ((feature = layer->GetNextFeature()) != nullptr) {
                OGRGeometry* geometry = feature->GetGeometryRef();
                if (!geometry) {
                    OGRFeature::DestroyFeature(feature);
                    continue;
                }

                const std::string fid = std::to_string(feature->GetFID());
                const OGRwkbGeometryType flatType = wkbFlatten(geometry->getGeometryType());
                if (rule.code == "C010101") {
                    if (hasWorkingExtent) {
                        OGREnvelope featureExtent{};
                        geometry->getEnvelope(&featureExtent);
                        if (!envelopeWithin(featureExtent, allowedExtent, tolerance)) {
                            issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid,
                                "要素范围{" + envelopeText(featureExtent) + "}超出工作范围{" + envelopeText(allowedExtent) + "}", source.path, geometry));
                        }
                    } else {
                        issues.push_back(makeIssue(issueIndex++, rule, layerName, fid,
                            "空间范围规则缺少有效工作范围参数：bbox 或 minX,minY,maxX,maxY"));
                    }
                }
                if ((needsSameLayerPairs || needsLineEndpointGraph) && geometry) {
                    const bool keep =
                        ((rule.code == "C030201" || rule.code == "C030202" || needsLineEndpointGraph) && isLineLike(geometry->getGeometryType())) ||
                        ((rule.code == "C030302" || rule.code == "C030301") && isPolygonLike(geometry->getGeometryType()));
                    if (keep) {
                        GeometrySnapshot snapshot;
                        snapshot.fid = fid;
                        snapshot.sourcePath = source.path;
                        snapshot.geometry.reset(geometry->clone());
                        pairCandidates.push_back(std::move(snapshot));
                    }
                }
                if (rule.code == "C020501" && geometry->getCoordinateDimension() >= 3) {
                    issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid, "要素包含 Z 值", source.path, geometry));
                } else if (rule.code == "C020201") {
                    double length = -1.0;
                    if (const auto* curve = dynamic_cast<const OGRCurve*>(geometry)) {
                        length = curve->get_Length();
                    } else if (const auto* multiCurve = dynamic_cast<const OGRMultiCurve*>(geometry)) {
                        length = multiCurve->get_Length();
                    }
                    if ((flatType == wkbLineString || flatType == wkbMultiLineString) && length >= 0.0 && length < tolerance) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid,
                            "线要素长度小于容差{" + std::to_string(tolerance) + "m}", source.path, geometry));
                    }
                } else if (rule.code == "C020301") {
                    const double minEdge = minPolygonSegmentLength(geometry);
                    if ((flatType == wkbPolygon || flatType == wkbMultiPolygon) && minEdge < tolerance) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid,
                            "面要素存在超短边，最短边长小于容差{" + std::to_string(tolerance) + "m}", source.path, geometry));
                    }
                } else if (rule.code == "C020302") {
                    if ((flatType == wkbPolygon || flatType == wkbMultiPolygon) && geometryHasSharpAngle(geometry, angleTolerance)) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid,
                            "面要素存在小于角度阈值{" + std::to_string(angleTolerance) + "°}的尖锐角", source.path, geometry));
                    }
                } else if (rule.code == "C020303") {
                    double area = -1.0;
                    if (const auto* surface = dynamic_cast<const OGRSurface*>(geometry)) {
                        area = surface->get_Area();
                    } else if (const auto* multiSurface = dynamic_cast<const OGRMultiSurface*>(geometry)) {
                        area = multiSurface->get_Area();
                    }
                    if ((flatType == wkbPolygon || flatType == wkbMultiPolygon) && area >= 0.0 && area < areaTolerance) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid,
                            "面要素面积小于阈值{" + std::to_string(areaTolerance) + "}", source.path, geometry));
                    }
                } else if (rule.code == "C020401") {
                    const int nodes = geometryNodeCount(geometry);
                    if (nodes > 0 && ((minNodes > 0 && nodes < minNodes) || (maxNodes > 0 && nodes > maxNodes))) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid,
                            "要素节点数量不符合要求，实际{" + std::to_string(nodes) + "}", source.path, geometry));
                    }
                } else if (rule.code == "C020402") {
                    if (isCurvedGeometryType(geometry->getGeometryType())) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid, "要素包含圆弧或曲线几何", source.path, geometry));
                    }
                } else if (rule.code == "C030101") {
                    if (flatType == wkbPoint) {
                        const auto* point = geometry->toPoint();
                        if (point) {
                            const long long x = static_cast<long long>(std::llround(point->getX() / tolerance));
                            const long long y = static_cast<long long>(std::llround(point->getY() / tolerance));
                            const std::string key = std::to_string(x) + "," + std::to_string(y);
                            const auto inserted = seenPoints.emplace(key, fid);
                            if (!inserted.second) {
                                issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid,
                                    "点要素与记录{" + inserted.first->second + "}在容差范围内重合", source.path, geometry));
                            }
                        }
                    }
                } else if (rule.code == "C030102" || rule.code == "C030208" || rule.code == "C030306") {
                    const auto* collection = dynamic_cast<const OGRGeometryCollection*>(geometry);
                    if (collection && collection->getNumGeometries() > 1) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid,
                            "要素包含多个部件，要求为单一部件", source.path, geometry));
                    }
                } else if (rule.code == "C030206" || rule.code == "C030207") {
                    if (isLineLike(geometry->getGeometryType()) && !geometry->IsSimple()) {
                        const std::string detail = rule.code == "C030206" ? "线要素可能存在自重叠" : "线要素可能存在自相交";
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid, detail, source.path, geometry));
                    }
                } else if (rule.code == "C030204") {
                    if (isLineLike(geometry->getGeometryType()) && geometryHasPseudoNode(geometry, tolerance)) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid, "线要素存在不改变线形的伪结点", source.path, geometry));
                    }
                } else if (rule.code == "C030304") {
                    if (isPolygonLike(geometry->getGeometryType()) && geometryHasPseudoNode(geometry, tolerance)) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid, "面要素存在不改变边界形状的伪结点", source.path, geometry));
                    }
                } else if (rule.code == "C030303") {
                    if ((flatType == wkbPolygon || flatType == wkbMultiPolygon) && !geometry->IsValid()) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid,
                            "面几何无效，可能存在自相交或环结构错误", source.path, geometry));
                    }
                } else if (rule.code == "C030305") {
                    const int holes = interiorRingCount(geometry);
                    if ((flatType == wkbPolygon || flatType == wkbMultiPolygon) && holes > 0) {
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, fid,
                            "面要素存在内部孔洞，孔洞数量{" + std::to_string(holes) + "}", source.path, geometry));
                    }
                }
                OGRFeature::DestroyFeature(feature);
            }

            if (needsLineEndpointGraph) {
                for (std::size_t i = 0; i < pairCandidates.size(); ++i) {
                    std::vector<OGRPoint> endpoints;
                    collectLineEndpoints(pairCandidates[i].geometry.get(), endpoints);
                    bool hasEndpointViolation = false;
                    for (const auto& endpoint : endpoints) {
                        const bool touchesOther = endpointTouchesAnotherLine(endpoint, pairCandidates, i, tolerance);
                        if ((rule.code == "C030203" && !touchesOther) ||
                            (rule.code == "C030205" && touchesOther)) {
                            hasEndpointViolation = true;
                            break;
                        }
                    }
                    if (hasEndpointViolation) {
                        const std::string detail = rule.code == "C030205"
                            ? "线要素端点仅连接一条其他线，形成连通性伪结点"
                            : "线要素存在未连接到同图层其他线端点的悬挂点";
                        issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, pairCandidates[i].fid, detail,
                            pairCandidates[i].sourcePath, pairCandidates[i].geometry.get()));
                    }
                }
            }

            if (needsSameLayerPairs) {
                for (std::size_t a = 0; a < pairCandidates.size(); ++a) {
                    for (std::size_t b = a + 1; b < pairCandidates.size(); ++b) {
                        const auto* left = pairCandidates[a].geometry.get();
                        const auto* right = pairCandidates[b].geometry.get();
                        if (!left || !right) {
                            continue;
                        }
                        if (rule.code == "C030301") {
                            if (left->Intersects(right) || left->Touches(right)) {
                                continue;
                            }
                            const double distance = left->Distance(right);
                            const double gapTolerance = parameterDouble(rule, "gapTolerance", tolerance);
                            if (distance > gapTolerance) {
                                issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, pairCandidates[b].fid,
                                    "面要素与记录{" + pairCandidates[a].fid + "}之间存在空隙，最近距离{" + std::to_string(distance) + "}",
                                    pairCandidates[b].sourcePath, pairCandidates[b].geometry.get()));
                            }
                            continue;
                        }
                        if (!left->Intersects(right)) {
                            continue;
                        }
                        std::unique_ptr<OGRGeometry> intersection(left->Intersection(right));
                        if (!intersection) {
                            continue;
                        }

                        if (rule.code == "C030201") {
                            const double overlapLength = geometryLinearLength(intersection.get());
                            if (overlapLength > tolerance || left->Equals(right)) {
                                issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, pairCandidates[b].fid,
                                    "线要素与记录{" + pairCandidates[a].fid + "}存在重叠",
                                    pairCandidates[b].sourcePath, pairCandidates[b].geometry.get()));
                            }
                        } else if (rule.code == "C030202") {
                            const double overlapLength = geometryLinearLength(intersection.get());
                            if (left->Crosses(right) || overlapLength > tolerance || left->Equals(right)) {
                                issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, pairCandidates[b].fid,
                                    "线要素与记录{" + pairCandidates[a].fid + "}存在相交或重叠",
                                    pairCandidates[b].sourcePath, pairCandidates[b].geometry.get()));
                            }
                        } else if (rule.code == "C030302") {
                            const double overlapArea = geometrySurfaceArea(intersection.get());
                            if (overlapArea > areaTolerance) {
                                issues.push_back(makeGeometryIssue(issueIndex++, rule, layerName, pairCandidates[b].fid,
                                    "面要素与记录{" + pairCandidates[a].fid + "}存在重叠，重叠面积{" + std::to_string(overlapArea) + "}",
                                    pairCandidates[b].sourcePath, pairCandidates[b].geometry.get()));
                            }
                        }
                    }
                }
            }
        }
        GDALClose(dataset);
    }
#else
    (void)rootPath;
    (void)rule;
    (void)issues;
    (void)issueIndex;
#endif
}

bool RuleCheckEngine::isDirectoryEmpty(const fs::path& path) {
    return fs::is_empty(path);
}

bool RuleCheckEngine::isSupportedDatasetEntry(const fs::directory_entry& entry) {
    auto ext = pathText(entry.path().extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return (entry.is_directory() && ext == ".gdb") || (entry.is_regular_file() && (ext == ".shp" || ext == ".gpkg"));
}

bool RuleCheckEngine::isRegularOrDirectory(const fs::path& path) {
    return fs::exists(path) && (fs::is_regular_file(path) || fs::is_directory(path));
}

std::string RuleCheckEngine::lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string RuleCheckEngine::trim(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

void RuleCheckEngine::appendInterLayerTopologyIssues(const fs::path& rootPath, const RuleDefinition& rule, std::vector<IssueRecord>& issues, int& issueIndex) {
#ifdef GISQC_HAVE_GDAL
    configureGdalRuntime();
    GDALAllRegister();

    const auto sourceLayerName = trim(rule.parameters.count("sourceLayer") ? rule.parameters.at("sourceLayer") : "");
    const auto referenceLayerName = trim(rule.parameters.count("referenceLayer") ? rule.parameters.at("referenceLayer") : "");
    const auto singleLayers = csvParameter(rule, "layers");
    const double tolerance = parameterDouble(rule, "tolerance", 0.001);

    // Scan for data sources
    DatasetScanner scanner;
    const auto scanned = scanner.scan(pathText(rootPath));

    // Helper: open a layer by name from available data sources
    std::string sourceDatasetPath;
    auto openLayer = [&](const std::string& layerName, std::string* openedPath = nullptr) -> std::pair<GDALDataset*, OGRLayer*> {
        for (const auto& source : scanned.sources) {
            if (source.type != DatasetType::FileGDB && source.type != DatasetType::GeoPackage) continue;
            auto* ds = static_cast<GDALDataset*>(GDALOpenEx(
                source.path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
            if (!ds) continue;
            OGRLayer* lyr = ds->GetLayerByName(layerName.c_str());
            if (lyr) {
                if (openedPath) *openedPath = source.path;
                return {ds, lyr};
            }
            GDALClose(ds);
        }
        return {nullptr, nullptr};
    };

    // Single-layer rules that still need a full spatial analysis engine.
    const bool isSingleLayer = false;

    if (isSingleLayer) {
        // These need a full spatial analysis engine (e.g. topology graph).
        // For now, log a placeholder issue indicating the check was requested.
        const std::string layerList = singleLayers.empty() ? "*" : rule.parameters.at("layers");
        issues.push_back(makeIssue(issueIndex++, rule, layerList, "-",
            "[待实现] 规则 " + rule.code + " (" + rule.name + ") 需要完整拓扑分析引擎支持"));
        (void)tolerance;
        return;
    }

    // Inter-layer rules: need sourceLayer and referenceLayer
    if (sourceLayerName.empty() || referenceLayerName.empty()) {
        issues.push_back(makeIssue(issueIndex++, rule, "-", "-",
            "跨图层规则 " + rule.code + " 缺少 sourceLayer 或 referenceLayer 参数"));
        return;
    }

    auto [srcDs, srcLayer] = openLayer(sourceLayerName, &sourceDatasetPath);
    if (!srcDs || !srcLayer) {
        issues.push_back(makeIssue(issueIndex++, rule, sourceLayerName, "-",
            "无法打开源图层：" + sourceLayerName));
        return;
    }

    auto [refDs, refLayer] = openLayer(referenceLayerName);
    if (!refDs || !refLayer) {
        issues.push_back(makeIssue(issueIndex++, rule, referenceLayerName, "-",
            "无法打开参照图层：" + referenceLayerName));
        GDALClose(srcDs);
        return;
    }

    // Collect reference geometries into a single collection for batch testing
    std::vector<std::unique_ptr<OGRGeometry>> refGeoms;
    refLayer->ResetReading();
    OGRFeature* refFeat = nullptr;
    while ((refFeat = refLayer->GetNextFeature()) != nullptr) {
        const OGRGeometry* g = refFeat->GetGeometryRef();
        if (g) refGeoms.emplace_back(g->clone());
        OGRFeature::DestroyFeature(refFeat);
    }

    // Build a union of reference geometries for contains/covered-by tests
    std::unique_ptr<OGRGeometry> refUnion;
    if (!refGeoms.empty()) {
        refUnion.reset(refGeoms[0]->clone());
        for (std::size_t i = 1; i < refGeoms.size(); ++i) {
            auto* merged = refUnion->Union(refGeoms[i].get());
            if (merged) refUnion.reset(merged);
        }
    }

    // Iterate source features and check topology relationship
    srcLayer->ResetReading();
    OGRFeature* srcFeat = nullptr;
    while ((srcFeat = srcLayer->GetNextFeature()) != nullptr) {
        const OGRGeometry* srcGeom = srcFeat->GetGeometryRef();
        const std::string fid = std::to_string(srcFeat->GetFID());
        if (!srcGeom || !refUnion) {
            OGRFeature::DestroyFeature(srcFeat);
            continue;
        }

        bool violation = false;
        std::string detail;

        // Determine check type from rule code
        if (rule.code == "C030103") {
            // 点-点：必须重合
            violation = !srcGeom->Intersects(refUnion.get());
            detail = "点要素未与参照图层的点重合";
        } else if (rule.code == "C030104") {
            auto* boundary = refUnion->Boundary();
            const auto* point = dynamic_cast<const OGRPoint*>(srcGeom);
            violation = !point || !pointCoveredByGeometry(*point, boundary, tolerance);
            if (boundary) OGRGeometryFactory::destroyGeometry(boundary);
            detail = "点要素未被参照线端点覆盖";
        } else if (rule.code == "C030105") {
            const auto* point = dynamic_cast<const OGRPoint*>(srcGeom);
            violation = !point || !pointCoveredByGeometry(*point, refUnion.get(), tolerance);
            detail = "点要素未被参照线图层覆盖";
        } else if (rule.code == "C030211") {
            // 线-线：源线必须被参照线覆盖
            violation = !geometryCoveredByGeometry(srcGeom, refUnion.get(), tolerance);
            detail = "线要素未被参照线图层覆盖";
        } else if (rule.code == "C030210") {
            violation = !lineEndpointsCoveredByGeometry(srcGeom, refUnion.get(), tolerance);
            detail = "线要素端点未被参照点图层覆盖";
        } else if (rule.code == "C030106") {
            // 点-面：点必须位于参照面边界上
            auto* boundary = refUnion->Boundary();
            violation = !boundary || srcGeom->Distance(boundary) > tolerance;
            if (boundary) {
                OGRGeometryFactory::destroyGeometry(boundary);
            }
            detail = "要素未位于参照图层边界上";
        } else if (rule.code == "C030107") {
            // 点-面：点必须严格位于面内部，边界点不通过
            violation = !refUnion->Contains(srcGeom);
            detail = "要素未位于参照图层面的内部";
        } else if (rule.code == "C030216") {
            // 线-面：线必须严格位于面内部，不能落在面边界上
            auto* boundary = refUnion->Boundary();
            const bool contained = refUnion->Contains(srcGeom);
            const bool touchesBoundary = boundary && srcGeom->Intersects(boundary);
            violation = !contained || touchesBoundary;
            if (boundary) OGRGeometryFactory::destroyGeometry(boundary);
            detail = "线要素未严格位于参照图层面的内部";
        } else if (rule.code == "C030212") {
            // 线-线：不得与参照线产生共线重叠；端点接触/单点相交不按重叠处理
            violation = lineOrPolygonOverlapExists(srcGeom, refUnion.get(), tolerance);
            detail = "线要素与参照线图层存在重叠";
        } else if (rule.code == "C030313") {
            // 不得重叠
            violation = lineOrPolygonOverlapExists(srcGeom, refUnion.get(), tolerance);
            detail = "面要素与参照图层存在重叠";
        } else if (rule.code == "C030213") {
            // 线-线：不得交叉或共线重叠；共享端点不算违规
            violation = srcGeom->Crosses(refUnion.get()) || srcGeom->Overlaps(refUnion.get()) || srcGeom->Within(refUnion.get());
            detail = "线要素与参照线图层存在相交";
        } else if (rule.code == "C030215") {
            // 线-面：必须被面边界覆盖
            auto* boundary = refUnion->Boundary();
            if (boundary) {
                violation = !geometryCoveredByGeometry(srcGeom, boundary, tolerance);
                OGRGeometryFactory::destroyGeometry(boundary);
            }
            detail = "线要素未被参照面图层边界覆盖";
        } else if (rule.code == "C030307") {
            // 面-点：面必须至少包含一个参照点（严格内部）
            bool anyContained = false;
            for (const auto& rg : refGeoms) {
                if (srcGeom->Contains(rg.get())) { anyContained = true; break; }
            }
            violation = !anyContained;
            detail = "面要素未包含参照图层的点";
        } else if (rule.code == "C030308") {
            // 面-点：面必须严格包含且仅包含一个参照点
            int containedCount = 0;
            for (const auto& rg : refGeoms) {
                if (srcGeom->Contains(rg.get())) { ++containedCount; }
            }
            violation = containedCount != 1;
            detail = "面要素未严格包含一个参照图层的点";
        } else if (rule.code == "C030309") {
            // 面-线：源面边界必须被参照线覆盖
            violation = !geometryBoundaryCoveredByGeometry(srcGeom, refUnion.get(), tolerance);
            detail = "面边界未被参照线图层覆盖";
        } else if (rule.code == "C030310") {
            // 面-面：源面必须被参照面覆盖
            violation = !geometryCoveredByGeometry(srcGeom, refUnion.get(), tolerance);
            detail = "面要素未被参照图层的面覆盖";
        } else if (rule.code == "C030312") {
            // 面-面：必须相互覆盖（同范围）
            violation = !geometryCoveredByGeometry(srcGeom, refUnion.get(), tolerance) ||
                        !geometryCoveredByGeometry(refUnion.get(), srcGeom, tolerance);
            detail = "面要素未与参照图层的面相互覆盖";
        } else if (rule.code == "C030314") {
            // 面-面：边界必须被参照面边界覆盖
            auto* srcBoundary = srcGeom->Boundary();
            auto* refBoundary = refUnion->Boundary();
            violation = !geometryCoveredByGeometry(srcBoundary, refBoundary, tolerance);
            if (srcBoundary) OGRGeometryFactory::destroyGeometry(srcBoundary);
            if (refBoundary) OGRGeometryFactory::destroyGeometry(refBoundary);
            detail = "面边界未被参照面图层的边界覆盖";
        }

        if (violation) {
            issues.push_back(makeGeometryIssue(issueIndex++, rule, sourceLayerName, fid, detail, sourceDatasetPath, srcGeom));
        }
        OGRFeature::DestroyFeature(srcFeat);
    }

    GDALClose(srcDs);
    GDALClose(refDs);
#else
    (void)rootPath;
    issues.push_back(makeIssue(issueIndex++, rule, "-", "-",
        "跨图层拓扑检查需要 GDAL 支持，当前未启用"));
#endif
}

} // namespace gisqc
