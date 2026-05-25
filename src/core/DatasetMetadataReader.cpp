#include "DatasetMetadataReader.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
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

#ifdef GISQC_HAVE_GDAL
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
#endif

} // namespace

DatasetMetadata DatasetMetadataReader::read(const DatasetSource& source) const {
    if (source.type == DatasetType::Shapefile) {
        return readShapefile(source);
    }

    if (source.type == DatasetType::GeoPackage || source.type == DatasetType::FileGDB) {
        return readGdalDataset(source);
    }

    DatasetMetadata metadata;
    metadata.status = "未知数据类型";
    metadata.messages.push_back(std::string(toString(source.type)) + " 暂不支持元数据读取");
    return metadata;
}

DatasetMetadata DatasetMetadataReader::readShapefile(const DatasetSource& source) {
    DatasetMetadata metadata;
    std::vector<std::string> missing;
    for (const auto& ext : {".shx", ".dbf"}) {
        if (!hasSidecar(source.path, ext)) {
            missing.push_back(ext);
        }
    }

    const std::string dbf = sidecarPath(source.path, ".dbf");
    if (fs::exists(dbf)) {
        const auto count = readDbfRecordCount(dbf);
        if (count >= 0) {
            metadata.featureCountKnown = true;
            metadata.featureCount = count;
            metadata.featureCountText = std::to_string(count);
        } else {
            metadata.featureCountText = "DBF无效";
            metadata.messages.push_back("无法读取 DBF 记录数：" + dbf);
        }
    } else {
        metadata.featureCountText = "缺少DBF";
    }

    const std::string prj = sidecarPath(source.path, ".prj");
    if (fs::exists(prj)) {
        metadata.crsText = summarizeProjection(readProjectionText(prj));
    } else {
        metadata.crsText = "缺少PRJ";
        metadata.messages.push_back("缺少坐标系文件：" + prj);
    }

    if (missing.empty() && metadata.messages.empty()) {
        metadata.status = "元数据已读取";
    } else {
        std::ostringstream status;
        status << "缺少配套文件";
        if (!missing.empty()) {
            status << ": ";
            for (std::size_t i = 0; i < missing.size(); ++i) {
                if (i > 0) status << ",";
                status << missing[i];
                metadata.messages.push_back("缺少 Shapefile 配套文件：" + sidecarPath(source.path, missing[i]));
            }
        }
        metadata.status = status.str();
    }
    return metadata;
}

DatasetMetadata DatasetMetadataReader::readGdalDataset(const DatasetSource& source) {
    DatasetMetadata metadata;
#ifdef GISQC_HAVE_GDAL
    configureGdalRuntime();
    GDALAllRegister();
    GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(source.path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (!dataset) {
        metadata.status = "GDAL打开失败";
        metadata.messages.push_back("GDAL 无法打开数据源：" + source.path);
        return metadata;
    }

    const int layerCount = dataset->GetLayerCount();
    if (layerCount <= 0) {
        metadata.status = "GDAL已读取：无图层";
        metadata.featureCountText = "0";
        metadata.featureCountKnown = true;
        metadata.featureCount = 0;
        metadata.crsText = "未发现图层坐标系";
        GDALClose(dataset);
        return metadata;
    }

    std::int64_t totalFeatures = 0;
    bool allCountsKnown = true;
    std::string firstCrs;
    std::vector<std::string> layerSummaries;

    for (int i = 0; i < layerCount; ++i) {
        OGRLayer* layer = dataset->GetLayer(i);
        if (!layer) continue;
        const char* layerName = layer->GetName();
        const GIntBig count = layer->GetFeatureCount(TRUE);
        if (count >= 0) {
            totalFeatures += static_cast<std::int64_t>(count);
        } else {
            allCountsKnown = false;
        }

        const OGRSpatialReference* srs = layer->GetSpatialRef();
        if (srs && firstCrs.empty()) {
            auto mutableSrs = srs->Clone();
            if (mutableSrs) {
                mutableSrs->AutoIdentifyEPSG();
            }
            const OGRSpatialReference* identifiedSrs = mutableSrs ? mutableSrs : srs;
            const char* authName = identifiedSrs->GetAuthorityName(nullptr);
            const char* authCode = identifiedSrs->GetAuthorityCode(nullptr);
            if (authCode && std::string(authCode) == "4490") {
                firstCrs = "CGCS2000 / WKID:4490";
            } else if (authCode && std::string(authCode) == "4326") {
                firstCrs = "WGS84 / WKID:4326";
            } else if (authName && authCode) {
                firstCrs = std::string(authName) + ":" + authCode;
            } else {
                const char* name = identifiedSrs->GetName();
                firstCrs = name ? std::string(name) : "已读取坐标系";
            }
            if (mutableSrs) {
                mutableSrs->Release();
            }
        }

        std::ostringstream layerText;
        layerText << "图层 " << (layerName ? layerName : "未命名") << "：";
        if (count >= 0) {
            layerText << count << " 要素";
        } else {
            layerText << "要素数未知";
        }
        layerSummaries.push_back(layerText.str());
    }

    metadata.featureCountKnown = allCountsKnown;
    metadata.featureCount = totalFeatures;
    metadata.featureCountText = allCountsKnown ? std::to_string(totalFeatures) : "GDAL已读取，部分图层未知";
    metadata.crsText = firstCrs.empty() ? "未发现坐标系" : firstCrs;
    metadata.status = "GDAL已读取：" + std::to_string(layerCount) + " 个图层";
    metadata.messages = layerSummaries;
    GDALClose(dataset);
#else
    metadata.status = "待接入GDAL";
    metadata.messages.push_back(std::string(toString(source.type)) + " 元数据读取需要 GDAL SDK");
#endif
    return metadata;
}

bool DatasetMetadataReader::hasSidecar(const std::string& shpPath, const std::string& extension) {
    return fs::exists(sidecarPath(shpPath, extension));
}

std::string DatasetMetadataReader::sidecarPath(const std::string& shpPath, const std::string& extension) {
    fs::path p(pathFromText(shpPath));
    p.replace_extension(extension);
    return pathText(p);
}

std::int64_t DatasetMetadataReader::readDbfRecordCount(const std::string& dbfPath) {
    std::ifstream in(pathFromText(dbfPath), std::ios::binary);
    if (!in) {
        return -1;
    }
    unsigned char header[8]{};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (in.gcount() != static_cast<std::streamsize>(sizeof(header))) {
        return -1;
    }
    return static_cast<std::int64_t>(header[4]) |
           (static_cast<std::int64_t>(header[5]) << 8) |
           (static_cast<std::int64_t>(header[6]) << 16) |
           (static_cast<std::int64_t>(header[7]) << 24);
}

std::string DatasetMetadataReader::readProjectionText(const std::string& prjPath) {
    std::ifstream in(pathFromText(prjPath));
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string DatasetMetadataReader::summarizeProjection(const std::string& projectionText) {
    std::string lower = projectionText;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("cgcs2000") != std::string::npos || lower.find("china_2000") != std::string::npos || lower.find("4490") != std::string::npos) {
        return "CGCS2000 / WKID:4490";
    }
    if (lower.find("wgs_1984") != std::string::npos || lower.find("wgs 84") != std::string::npos || lower.find("4326") != std::string::npos) {
        return "WGS84 / WKID:4326";
    }
    return projectionText.empty() ? "PRJ为空" : "已读取PRJ";
}

} // namespace gisqc
