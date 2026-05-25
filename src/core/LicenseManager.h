#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace gisqc {

struct LicenseInfo {
    std::string product;
    std::string machineCode;
    std::string expireDate;
    int maxRuns = 0;
    int usedRuns = 0;
    std::string issuedAt;
    std::string signature;
};

struct LicenseStatus {
    bool valid = false;
    bool expired = false;
    bool machineMismatch = false;
    bool runLimitExceeded = false;
    bool missing = false;
    std::string message;
    LicenseInfo license;
};

class LicenseManager {
public:
    static constexpr const char* ProductName = "GISQCWorkbench";

    static std::string machineCode();
    static std::filesystem::path defaultLicensePath();
    static std::filesystem::path defaultStatePath();
    static std::filesystem::path localAppDataDirectory();

    static std::uint64_t fnv1a64(const std::string& text);
    static std::string signatureFor(const std::string& product,
                                    const std::string& machineCode,
                                    const std::string& expireDate,
                                    int maxRuns,
                                    const std::string& issuedAt);

    static std::string generateLicenseText(const std::string& machineCode,
                                           const std::string& expireDate,
                                           int maxRuns,
                                           const std::string& issuedAt = currentDate());

    static LicenseStatus check(const std::filesystem::path& licensePath = defaultLicensePath(),
                               const std::filesystem::path& statePath = defaultStatePath(),
                               bool consumeRun = false);

    static bool installLicenseText(const std::string& licenseText,
                                   const std::filesystem::path& licensePath = defaultLicensePath());

    static std::string currentDate();
    static bool isDateExpired(const std::string& yyyyMmDd);

private:
    static LicenseInfo parseLicense(const std::string& text);
    static std::string serializeLicense(const LicenseInfo& info);
};

} // namespace gisqc
