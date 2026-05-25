#pragma once

#include "DatasetScanner.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gisqc {

struct DatasetMetadata {
    bool featureCountKnown{false};
    std::int64_t featureCount{0};
    std::string featureCountText{"待接入GDAL"};
    std::string crsText{"待接入GDAL"};
    std::string status{"待扫描"};
    std::vector<std::string> messages;
};

class DatasetMetadataReader {
public:
    DatasetMetadata read(const DatasetSource& source) const;

private:
    static DatasetMetadata readShapefile(const DatasetSource& source);
    static DatasetMetadata readGdalDataset(const DatasetSource& source);
    static bool hasSidecar(const std::string& shpPath, const std::string& extension);
    static std::string sidecarPath(const std::string& shpPath, const std::string& extension);
    static std::int64_t readDbfRecordCount(const std::string& dbfPath);
    static std::string readProjectionText(const std::string& prjPath);
    static std::string summarizeProjection(const std::string& projectionText);
};

} // namespace gisqc
