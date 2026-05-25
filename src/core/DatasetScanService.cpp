#include "DatasetScanService.h"
#include "DatasetMetadataReader.h"
#include "DatasetScanner.h"

namespace gisqc {

DatasetScanSummary DatasetScanService::scan(const std::string& rootPath) const {
    DatasetScanner scanner;
    DatasetMetadataReader metadataReader;
    const auto result = scanner.scan(rootPath);

    DatasetScanSummary summary;
    summary.rootPath = result.rootPath;
    summary.sourceCount = static_cast<int>(result.sources.size());
    summary.logs = result.messages;

    for (const auto& source : result.sources) {
        const auto metadata = metadataReader.read(source);
        DatasetTableRow row;
        row.name = source.displayName;
        row.type = toString(source.type);
        row.featureCountText = metadata.featureCountText;
        row.crsText = metadata.crsText;
        row.status = metadata.status;
        row.path = source.path;
        summary.rows.push_back(row);
        summary.logs.insert(summary.logs.end(), metadata.messages.begin(), metadata.messages.end());
    }

    return summary;
}

} // namespace gisqc
