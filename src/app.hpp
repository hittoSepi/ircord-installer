#pragma once

#include "install/installer.hpp"

#include <memory>
#include <string>

namespace ircord::installer {

class InstallerApp {
public:
    InstallerApp(SystemInfo system_info,
                 Manifest manifest,
                 std::string manifest_source,
                 std::string manifest_error = {});
    ~InstallerApp();

    int Run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ircord::installer
