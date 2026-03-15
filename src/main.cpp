#include "app.hpp"
#include "manifest.hpp"
#include "system/system.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintUsage(const char* argv0) {
    std::cout
        << "Usage: " << argv0 << " [--manifest-url <url>] [--manifest-file <path>]\n"
        << "  --manifest-url   Download manifest JSON before starting the installer\n"
        << "  --manifest-file  Read manifest JSON from a local file\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    using namespace ircord::installer;

    std::string manifest_url;
    std::filesystem::path manifest_file;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }
        if (arg == "--manifest-url" && i + 1 < argc) {
            manifest_url = argv[++i];
            continue;
        }
        if (arg == "--manifest-file" && i + 1 < argc) {
            manifest_file = argv[++i];
            continue;
        }

        std::cerr << "Unknown argument: " << arg << "\n";
        PrintUsage(argv[0]);
        return 1;
    }

    const auto system_info = SystemOps::DetectSystem();
    std::string manifest_error;

    if (manifest_file.empty()) {
        const auto temp_dir = SystemOps::CreateTempDir();
        manifest_file = temp_dir / "installer-manifest.json";
        if (manifest_url.empty()) {
            manifest_url = "https://chat.rausku.com/downloads/installer-manifest.json";
        }
        if (!SystemOps::DownloadFile(manifest_url, manifest_file, &manifest_error)) {
            std::cerr << "Failed to download manifest: " << manifest_error << "\n";
            return 1;
        }
    }

    const auto manifest = LoadManifestFromFile(manifest_file, system_info.platform_key, &manifest_error);
    if (!manifest.has_value()) {
        std::cerr << "Failed to load manifest: " << manifest_error << "\n";
        return 1;
    }

    InstallerApp app(system_info, *manifest, manifest_file.string(), manifest_error);
    return app.Run();
}
