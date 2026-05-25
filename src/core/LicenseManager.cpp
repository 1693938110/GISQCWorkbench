#include "LicenseManager.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <Lmcons.h>
#include <Shlwapi.h>
#endif

namespace gisqc {
namespace {

constexpr const char* LicenseSecret = "GISQC-PRIVATE-KEY-2026-05-NOT-FOR-CLIENT-CHANGE";

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string upperHex(std::uint64_t value) {
    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << value;
    return ss.str();
}

std::string formatMachineCode(std::uint64_t value) {
    const auto raw = upperHex(value);
    return raw.substr(0, 4) + "-" + raw.substr(4, 4) + "-" + raw.substr(8, 4) + "-" + raw.substr(12, 4);
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("无法写入文件：" + path.u8string());
    }
    out << text;
}

std::map<std::string, std::string> parseKeyValues(const std::string& text) {
    std::map<std::string, std::string> values;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        values[trim(line.substr(0, pos))] = trim(line.substr(pos + 1));
    }
    return values;
}

int toInt(const std::string& value, int defaultValue = 0) {
    try {
        return std::stoi(value);
    } catch (...) {
        return defaultValue;
    }
}

constexpr const char* RegistryStateKey = "Software\\GISQC\\WorkbenchState";
constexpr const char* StateIntegritySecret = "GISQC-STATE-INTEGRITY-2026-NOT-CLIENT";

std::string stateChecksum(const std::string& machineCode, int usedRuns) {
    std::string payload = machineCode + "|" + std::to_string(usedRuns) + "|" + StateIntegritySecret;
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : payload) { hash ^= c; hash *= 1099511628211ull; }
    std::ostringstream ss;
    ss << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << hash;
    return ss.str();
}

#ifdef _WIN32
int readStateFromRegistry(const std::string& machineCode) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, RegistryStateKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return 0;
    auto closeKey = [&]() { if (hKey) RegCloseKey(hKey); };

    DWORD runsDword = 0;
    DWORD size = sizeof(runsDword);
    if (RegQueryValueExA(hKey, "UsedRuns", nullptr, nullptr, reinterpret_cast<LPBYTE>(&runsDword), &size) != ERROR_SUCCESS) {
        closeKey(); return 0;
    }

    char checksumBuf[64]{};
    DWORD checksumSize = sizeof(checksumBuf);
    if (RegQueryValueExA(hKey, "StateHash", nullptr, nullptr, reinterpret_cast<LPBYTE>(checksumBuf), &checksumSize) != ERROR_SUCCESS) {
        closeKey(); return 0;
    }
    closeKey();

    const int usedRuns = static_cast<int>(runsDword);
    const std::string storedHash(checksumBuf);
    const std::string expectedHash = stateChecksum(machineCode, usedRuns);
    if (storedHash != expectedHash) return 0; // tampered
    return usedRuns;
}

void writeStateToRegistry(const std::string& machineCode, int usedRuns) {
    HKEY hKey = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, RegistryStateKey, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, &disposition) != ERROR_SUCCESS) return;

    DWORD runsDword = static_cast<DWORD>(usedRuns);
    RegSetValueExA(hKey, "UsedRuns", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&runsDword), sizeof(runsDword));

    const std::string hash = stateChecksum(machineCode, usedRuns);
    RegSetValueExA(hKey, "StateHash", 0, REG_SZ, reinterpret_cast<const BYTE*>(hash.c_str()), static_cast<DWORD>(hash.size() + 1));

    const auto date = LicenseManager::currentDate();
    RegSetValueExA(hKey, "LastRun", 0, REG_SZ, reinterpret_cast<const BYTE*>(date.c_str()), static_cast<DWORD>(date.size() + 1));

    RegCloseKey(hKey);
}
#else
int readStateFromRegistry(const std::string&) { return 0; }
void writeStateToRegistry(const std::string&, int) {}
#endif

std::string userMachineSeed() {
#ifdef _WIN32
    char computer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD computerSize = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameA(computer, &computerSize);

    char user[UNLEN + 1]{};
    DWORD userSize = UNLEN + 1;
    GetUserNameA(user, &userSize);

    char windowsDir[MAX_PATH]{};
    GetWindowsDirectoryA(windowsDir, MAX_PATH);

    DWORD serial = 0;
    GetVolumeInformationA("C:\\\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);

    std::ostringstream ss;
    ss << computer << '|' << user << '|' << windowsDir << '|' << std::hex << serial;
    return ss.str();
#else
    const char* user = std::getenv("USER");
    const char* host = std::getenv("HOSTNAME");
    return std::string(user ? user : "unknown") + "|" + (host ? host : "unknown");
#endif
}

} // namespace

std::uint64_t LicenseManager::fnv1a64(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string LicenseManager::machineCode() {
    return formatMachineCode(fnv1a64(userMachineSeed()));
}

std::filesystem::path LicenseManager::localAppDataDirectory() {
#ifdef _WIN32
    if (const char* appData = std::getenv("LOCALAPPDATA")) {
        return std::filesystem::path(appData) / "GISQCWorkbench";
    }
#endif
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".gisqc-workbench";
    }
    return std::filesystem::current_path() / ".gisqc-workbench";
}

std::filesystem::path LicenseManager::defaultLicensePath() {
#ifdef _WIN32
    wchar_t modulePath[MAX_PATH]{};
    const DWORD size = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (size > 0 && size < MAX_PATH) {
        const auto exeLicense = std::filesystem::path(modulePath).parent_path() / "license.dat";
        if (std::filesystem::exists(exeLicense)) {
            return exeLicense;
        }
    }
#endif
    const auto cwdLicense = std::filesystem::current_path() / "license.dat";
    if (std::filesystem::exists(cwdLicense)) {
        return cwdLicense;
    }
    return localAppDataDirectory() / "license.dat";
}

std::filesystem::path LicenseManager::defaultStatePath() {
    return localAppDataDirectory() / "license.state";
}

std::string LicenseManager::currentDate() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d");
    return ss.str();
}

bool LicenseManager::isDateExpired(const std::string& yyyyMmDd) {
    if (yyyyMmDd.empty() || yyyyMmDd.size() < 10) {
        return true;
    }
    return yyyyMmDd < currentDate();
}

std::string LicenseManager::signatureFor(const std::string& product,
                                         const std::string& machineCode,
                                         const std::string& expireDate,
                                         int maxRuns,
                                         const std::string& issuedAt) {
    const std::string payload = product + "|" + machineCode + "|" + expireDate + "|" + std::to_string(maxRuns) + "|" + issuedAt + "|" + LicenseSecret;
    const auto h1 = fnv1a64(payload);
    const auto h2 = fnv1a64(LicenseSecret + payload + machineCode);
    return upperHex(h1) + upperHex(h2);
}

std::string LicenseManager::generateLicenseText(const std::string& machineCode,
                                                const std::string& expireDate,
                                                int maxRuns,
                                                const std::string& issuedAt) {
    LicenseInfo info;
    info.product = ProductName;
    info.machineCode = machineCode;
    info.expireDate = expireDate;
    info.maxRuns = maxRuns;
    info.usedRuns = 0;
    info.issuedAt = issuedAt;
    info.signature = signatureFor(info.product, info.machineCode, info.expireDate, info.maxRuns, info.issuedAt);
    return serializeLicense(info);
}

LicenseInfo LicenseManager::parseLicense(const std::string& text) {
    const auto values = parseKeyValues(text);
    LicenseInfo info;
    if (auto it = values.find("product"); it != values.end()) info.product = it->second;
    if (auto it = values.find("machineCode"); it != values.end()) info.machineCode = it->second;
    if (auto it = values.find("expireDate"); it != values.end()) info.expireDate = it->second;
    if (auto it = values.find("maxRuns"); it != values.end()) info.maxRuns = toInt(it->second);
    if (auto it = values.find("usedRuns"); it != values.end()) info.usedRuns = toInt(it->second);
    if (auto it = values.find("issuedAt"); it != values.end()) info.issuedAt = it->second;
    if (auto it = values.find("signature"); it != values.end()) info.signature = it->second;
    return info;
}

std::string LicenseManager::serializeLicense(const LicenseInfo& info) {
    std::ostringstream out;
    out << "product=" << info.product << '\n'
        << "machineCode=" << info.machineCode << '\n'
        << "expireDate=" << info.expireDate << '\n'
        << "maxRuns=" << info.maxRuns << '\n'
        << "issuedAt=" << info.issuedAt << '\n'
        << "signature=" << info.signature << '\n';
    return out.str();
}

bool LicenseManager::installLicenseText(const std::string& licenseText, const std::filesystem::path& licensePath) {
    const auto info = parseLicense(licenseText);
    const auto expected = signatureFor(info.product, info.machineCode, info.expireDate, info.maxRuns, info.issuedAt);
    if (info.product != ProductName || info.signature != expected) {
        return false;
    }
    writeTextFile(licensePath, serializeLicense(info));
    return true;
}

LicenseStatus LicenseManager::check(const std::filesystem::path& licensePath,
                                    const std::filesystem::path& statePath,
                                    bool consumeRun) {
    LicenseStatus status;
    const auto text = readTextFile(licensePath);
    if (text.empty()) {
        status.missing = true;
        status.message = "未找到授权文件 license.dat。机器码：" + machineCode();
        return status;
    }

    status.license = parseLicense(text);
    const auto expected = signatureFor(status.license.product,
                                       status.license.machineCode,
                                       status.license.expireDate,
                                       status.license.maxRuns,
                                       status.license.issuedAt);
    if (status.license.product != ProductName || status.license.signature != expected) {
        status.message = "授权文件签名无效或已被篡改。";
        return status;
    }
    if (status.license.machineCode != machineCode()) {
        status.machineMismatch = true;
        status.message = "授权机器码不匹配。当前机器码：" + machineCode();
        return status;
    }
    if (isDateExpired(status.license.expireDate)) {
        status.expired = true;
        status.message = "授权已过期，有效期至：" + status.license.expireDate;
        return status;
    }

    // Read run state from registry (with integrity check)
    int usedRuns = readStateFromRegistry(status.license.machineCode);
    // Fallback: also read legacy file state (migration)
    if (usedRuns <= 0) {
        const auto stateText = readTextFile(statePath);
        const auto state = parseKeyValues(stateText);
        if (auto it = state.find("usedRuns"); it != state.end()) {
            usedRuns = toInt(it->second);
        }
    }
    status.license.usedRuns = usedRuns;

    if (status.license.maxRuns > 0 && usedRuns >= status.license.maxRuns) {
        status.runLimitExceeded = true;
        status.message = "授权运行次数已用完：" + std::to_string(usedRuns) + "/" + std::to_string(status.license.maxRuns);
        return status;
    }

    if (consumeRun) {
        ++usedRuns;
        writeStateToRegistry(status.license.machineCode, usedRuns);
        // Also write legacy file for backwards compat
        std::ostringstream ss;
        ss << "machineCode=" << status.license.machineCode << '\n'
           << "usedRuns=" << usedRuns << '\n'
           << "lastRun=" << currentDate() << '\n';
        writeTextFile(statePath, ss.str());
        status.license.usedRuns = usedRuns;
    }

    status.valid = true;
    status.message = "授权有效，有效期至 " + status.license.expireDate + "，运行次数 " + std::to_string(status.license.usedRuns) + "/" + (status.license.maxRuns > 0 ? std::to_string(status.license.maxRuns) : std::string("不限"));
    return status;
}

} // namespace gisqc
