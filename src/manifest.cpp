#include "manifest.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace ircord::installer {

namespace {

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to open manifest file: " + path.string());
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

ArtifactInfo ParseArtifact(const nlohmann::json& object, const char* key) {
    if (!object.contains(key)) {
        throw std::runtime_error(std::string("Manifest missing artifact: ") + key);
    }

    const auto& artifact = object.at(key);
    return ArtifactInfo{
        artifact.at("url").get<std::string>(),
        artifact.value("sha256", ""),
    };
}

}  // namespace

std::optional<Manifest> LoadManifestFromFile(const std::filesystem::path& path,
                                             const std::string& platform_key,
                                             std::string* error_message) {
    try {
        const auto parsed = nlohmann::json::parse(ReadFile(path));
        const auto& platforms = parsed.at("platforms");
        const auto& platform = platforms.at(platform_key);

        return Manifest{
            parsed.value("version", "unknown"),
            parsed.value("docs_url", "https://chat.rausku.com/docs"),
            ParseArtifact(platform, "installer"),
            ParseArtifact(platform, "server"),
        };
    } catch (const std::exception& ex) {
        if (error_message != nullptr) {
            *error_message = ex.what();
        }
        return std::nullopt;
    }
}

}  // namespace ircord::installer
