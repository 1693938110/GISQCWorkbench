#pragma once

#include <string>
#include <vector>

namespace gisqc {

enum class DatasetType {
    FileGDB,
    Shapefile,
    GeoPackage,
    Unknown
};

struct DatasetSource {
    DatasetType type{DatasetType::Unknown};
    std::string displayName;
    std::string layerName;
    std::string path;
    std::string status;
};

struct ScanResult {
    std::string rootPath;
    std::vector<DatasetSource> sources;
    std::vector<std::string> messages;
};

class DatasetScanner {
public:
    ScanResult scan(const std::string& rootPath) const;

private:
    static std::string extensionLower(const std::string& path);
    static std::string filename(const std::string& path);
    static std::string stem(const std::string& path);
};

const char* toString(DatasetType type);

} // namespace gisqc
