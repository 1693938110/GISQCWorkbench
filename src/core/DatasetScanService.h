#pragma once

#include <string>
#include <vector>

namespace gisqc {

struct DatasetTableRow {
    std::string name;
    std::string type;
    std::string featureCountText;
    std::string crsText;
    std::string status;
    std::string path;
};

struct DatasetScanSummary {
    std::string rootPath;
    int sourceCount{0};
    std::vector<DatasetTableRow> rows;
    std::vector<std::string> logs;
};

class DatasetScanService {
public:
    DatasetScanSummary scan(const std::string& rootPath) const;
};

} // namespace gisqc
