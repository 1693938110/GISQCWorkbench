#include "DatasetScanner.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace gisqc {

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

fs::path pathFromText(const std::string& text) {
#ifdef _WIN32
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return fs::path(text);
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
    wide.resize(static_cast<std::size_t>(size - 1));
    return fs::path(wide);
#else
    return fs::u8path(text);
#endif
}

} // namespace

ScanResult DatasetScanner::scan(const std::string& rootPath) const {
    ScanResult result;
    result.rootPath = rootPath;

    fs::path root(pathFromText(rootPath));
    if (!fs::exists(root)) {
        result.messages.push_back("路径不存在：" + rootPath);
        return result;
    }

    auto addIfSupported = [&](const fs::directory_entry& entry) {
        const auto path = entry.path();
        DatasetSource source;

        const auto pathString = pathText(path);
        if (entry.is_directory() && extensionLower(pathString) == ".gdb") {
            source.type = DatasetType::FileGDB;
            source.displayName = filename(pathString);
            source.layerName = source.displayName;
        } else if (entry.is_regular_file() && extensionLower(pathString) == ".shp") {
            source.type = DatasetType::Shapefile;
            source.displayName = filename(pathString);
            source.layerName = stem(pathString);
        } else if (entry.is_regular_file() && extensionLower(pathString) == ".gpkg") {
            source.type = DatasetType::GeoPackage;
            source.displayName = filename(pathString);
            source.layerName = stem(pathString);
        } else {
            return;
        }

        source.path = pathString;
        source.status = "待扫描";
        result.sources.push_back(source);
    };

    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        addIfSupported(*it);
        if (it->is_directory() && extensionLower(pathText(it->path())) == ".gdb") {
            it.disable_recursion_pending();
        }
    }

    std::sort(result.sources.begin(), result.sources.end(), [](const auto& a, const auto& b) {
        if (a.type != b.type) {
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        }
        return a.displayName < b.displayName;
    });

    result.messages.push_back("发现数据源：" + std::to_string(result.sources.size()) + " 个");
    return result;
}

std::string DatasetScanner::extensionLower(const std::string& path) {
    std::string ext = pathText(pathFromText(path).extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext;
}

std::string DatasetScanner::filename(const std::string& path) {
    return pathText(pathFromText(path).filename());
}

std::string DatasetScanner::stem(const std::string& path) {
    return pathText(pathFromText(path).stem());
}

const char* toString(DatasetType type) {
    switch (type) {
    case DatasetType::FileGDB:
        return "FileGDB";
    case DatasetType::Shapefile:
        return "Shapefile";
    case DatasetType::GeoPackage:
        return "GeoPackage";
    default:
        return "Unknown";
    }
}

} // namespace gisqc
