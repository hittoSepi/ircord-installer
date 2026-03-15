#pragma once

#include "../model.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace ircord::installer {

struct CommandResult {
    int exit_code = 0;
    std::string output;
};

class SystemOps {
public:
    static SystemInfo DetectSystem();
    static std::optional<uint16_t> ParsePort(const std::string& value);
    static bool PathWritable(const std::filesystem::path& path, std::string* reason);
    static bool PortAvailable(const std::string& listen_address, uint16_t port, std::string* reason);
    static std::uintmax_t FreeSpaceBytes(const std::filesystem::path& path);

    static CommandResult RunShell(const std::string& command);
    static bool CommandExists(const std::string& command);
    static bool DownloadFile(const std::string& url, const std::filesystem::path& destination, std::string* error);
    static bool VerifySha256(const std::filesystem::path& path, const std::string& expected_sha256, std::string* error);
    static std::filesystem::path CreateTempDir();
    static bool ExtractTarGz(const std::filesystem::path& archive_path,
                             const std::filesystem::path& destination,
                             std::string* error);
    static std::filesystem::path FindFileRecursive(const std::filesystem::path& root, const std::string& filename);

    static std::string ShellEscape(const std::string& input);
};

}  // namespace ircord::installer
