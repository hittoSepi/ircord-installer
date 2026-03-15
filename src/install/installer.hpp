#pragma once

#include "../model.hpp"

#include <functional>
#include <string>
#include <vector>

namespace ircord::installer {

class InstallerRunner {
public:
    using ProgressCallback = std::function<void(const std::string&, bool)>;

    InstallerRunner(SystemInfo system_info, Manifest manifest);

    std::vector<PreflightIssue> RunPreflight(const InstallConfig& config) const;
    InstallSummary RunInstall(const InstallConfig& config, const ProgressCallback& callback) const;

private:
    SystemInfo system_info_;
    Manifest manifest_;
};

}  // namespace ircord::installer
