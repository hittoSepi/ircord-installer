#include "system.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ircord::installer {

namespace {

std::string ReadOsReleaseValue(const std::string& key) {
    std::ifstream input("/etc/os-release");
    if (!input) {
        return {};
    }

    std::string line;
    while (std::getline(input, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        if (line.substr(0, pos) != key) {
            continue;
        }

        auto value = line.substr(pos + 1);
        if (!value.empty() && value.front() == '"') {
            value.erase(value.begin());
        }
        if (!value.empty() && value.back() == '"') {
            value.pop_back();
        }
        return value;
    }

    return {};
}

std::string NormalizeArch(std::string raw) {
    while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r')) {
        raw.pop_back();
    }
    if (raw == "x86_64" || raw == "amd64") {
        return "x64";
    }
    if (raw == "aarch64" || raw == "arm64") {
        return "arm64";
    }
    return raw;
}

std::string CaptureOutput(const std::string& command, int* exit_code) {
    std::array<char, 4096> buffer{};
    std::string output;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        if (exit_code != nullptr) {
            *exit_code = -1;
        }
        return "Failed to launch command";
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    if (exit_code != nullptr) {
        *exit_code = status;
    }
    return output;
}

std::filesystem::path ExistingParent(std::filesystem::path path) {
    if (path.empty()) {
        return std::filesystem::current_path();
    }
    while (!path.empty() && !std::filesystem::exists(path)) {
        path = path.parent_path();
    }
    if (path.empty()) {
        return std::filesystem::path("/");
    }
    return path;
}

}  // namespace

SystemInfo SystemOps::DetectSystem() {
    SystemInfo info;

#ifdef __linux__
    info.is_linux = true;
    info.is_root = (::geteuid() == 0);
    info.has_systemd = std::filesystem::exists("/run/systemd/system");
    info.has_ufw = CommandExists("ufw");
    info.has_apt = CommandExists("apt-get");
    info.distro_id = ReadOsReleaseValue("ID");
    info.distro_version = ReadOsReleaseValue("VERSION_ID");
    info.arch = NormalizeArch(RunShell("uname -m").output);
    info.platform_key = "linux-" + info.arch;
#else
    info.arch = "unsupported";
    info.platform_key = "unsupported";
#endif

    return info;
}

std::optional<uint16_t> SystemOps::ParsePort(const std::string& value) {
    try {
        const int parsed = std::stoi(value);
        if (parsed < 1 || parsed > 65535) {
            return std::nullopt;
        }
        return static_cast<uint16_t>(parsed);
    } catch (...) {
        return std::nullopt;
    }
}

bool SystemOps::PathWritable(const std::filesystem::path& path, std::string* reason) {
    try {
        const auto parent = ExistingParent(path);
#ifdef __linux__
        if (::access(parent.c_str(), W_OK) == 0) {
            return true;
        }
        if (reason != nullptr) {
            *reason = "Path is not writable: " + parent.string();
        }
        return false;
#else
        if (reason != nullptr) {
            *reason = "Writable path checks are only implemented on Linux";
        }
        return false;
#endif
    } catch (const std::exception& ex) {
        if (reason != nullptr) {
            *reason = ex.what();
        }
        return false;
    }
}

bool SystemOps::PortAvailable(const std::string& listen_address, uint16_t port, std::string* reason) {
#ifdef __linux__
    const int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        if (reason != nullptr) {
            *reason = "Unable to create socket for port check";
        }
        return false;
    }

    const int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (listen_address == "0.0.0.0" || listen_address == "*" || listen_address.empty()) {
        address.sin_addr.s_addr = INADDR_ANY;
    } else if (::inet_pton(AF_INET, listen_address.c_str(), &address.sin_addr) != 1) {
        ::close(sock);
        if (reason != nullptr) {
            *reason = "Unsupported listen address for port check: " + listen_address;
        }
        return false;
    }

    const bool success = (::bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    ::close(sock);
    if (!success && reason != nullptr) {
        *reason = "Port is already in use: " + std::to_string(port);
    }
    return success;
#else
    (void)listen_address;
    (void)port;
    if (reason != nullptr) {
        *reason = "Port checks are only implemented on Linux";
    }
    return false;
#endif
}

std::uintmax_t SystemOps::FreeSpaceBytes(const std::filesystem::path& path) {
    const auto parent = ExistingParent(path);
    return std::filesystem::space(parent).available;
}

CommandResult SystemOps::RunShell(const std::string& command) {
    int exit_code = 0;
    auto output = CaptureOutput(command, &exit_code);
    return CommandResult{exit_code, std::move(output)};
}

bool SystemOps::CommandExists(const std::string& command) {
    const auto result = RunShell("command -v " + ShellEscape(command));
    return result.exit_code == 0;
}

bool SystemOps::DownloadFile(const std::string& url, const std::filesystem::path& destination, std::string* error) {
    std::filesystem::create_directories(destination.parent_path());
    const auto result = RunShell(
        "curl -fL --silent --show-error --output " + ShellEscape(destination.string()) +
        " " + ShellEscape(url));
    if (result.exit_code == 0) {
        return true;
    }
    if (error != nullptr) {
        *error = result.output;
    }
    return false;
}

bool SystemOps::VerifySha256(const std::filesystem::path& path, const std::string& expected_sha256, std::string* error) {
    if (expected_sha256.empty()) {
        return true;
    }

    const auto result = RunShell("sha256sum " + ShellEscape(path.string()));
    if (result.exit_code != 0) {
        if (error != nullptr) {
            *error = result.output;
        }
        return false;
    }

    const auto separator = result.output.find(' ');
    const std::string actual = result.output.substr(0, separator);
    if (actual == expected_sha256) {
        return true;
    }

    if (error != nullptr) {
        *error = "SHA256 mismatch. Expected " + expected_sha256 + ", got " + actual;
    }
    return false;
}

std::filesystem::path SystemOps::CreateTempDir() {
    auto base = std::filesystem::temp_directory_path();
    std::random_device rd;
    std::uniform_int_distribution<unsigned int> dist(0, 0xFFFFFF);

    for (int attempt = 0; attempt < 32; ++attempt) {
        auto candidate = base / ("ircord-installer-" + std::to_string(dist(rd)));
        if (!std::filesystem::exists(candidate)) {
            std::filesystem::create_directories(candidate);
            return candidate;
        }
    }

    throw std::runtime_error("Failed to allocate temporary directory");
}

bool SystemOps::ExtractTarGz(const std::filesystem::path& archive_path,
                             const std::filesystem::path& destination,
                             std::string* error) {
    std::filesystem::create_directories(destination);
    const auto result = RunShell(
        "tar -xzf " + ShellEscape(archive_path.string()) + " -C " + ShellEscape(destination.string()));
    if (result.exit_code == 0) {
        return true;
    }
    if (error != nullptr) {
        *error = result.output;
    }
    return false;
}

std::filesystem::path SystemOps::FindFileRecursive(const std::filesystem::path& root, const std::string& filename) {
    if (!std::filesystem::exists(root)) {
        return {};
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().filename() == filename) {
            return entry.path();
        }
    }
    return {};
}

std::string SystemOps::ShellEscape(const std::string& input) {
    std::string escaped = "'";
    for (char ch : input) {
        if (ch == '\'') {
            escaped += "'\"'\"'";
        } else {
            escaped += ch;
        }
    }
    escaped += "'";
    return escaped;
}

}  // namespace ircord::installer
