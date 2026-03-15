#include "app.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace ircord::installer {

using namespace ftxui;

namespace {

Element BoxTitle(const std::string& text_value) {
    return text(text_value) | bold;
}

Element ParagraphLines(const std::vector<std::string>& lines) {
    Elements children;
    for (const auto& line : lines) {
        children.push_back(paragraph(line));
    }
    if (children.empty()) {
        children.push_back(text(""));
    }
    return vbox(std::move(children));
}

std::vector<std::string> FormatPreflight(const std::vector<PreflightIssue>& issues) {
    std::vector<std::string> lines;
    if (issues.empty()) {
        lines.push_back("[ok] Preflight passed with current settings.");
        return lines;
    }

    for (const auto& issue : issues) {
        lines.push_back(std::string(issue.blocking ? "[block] " : "[warn] ") + issue.message);
    }
    return lines;
}

std::string TlsLabel(TlsMode mode) {
    switch (mode) {
        case TlsMode::LetsEncrypt:
            return "Let's Encrypt";
        case TlsMode::SelfSigned:
            return "Self-signed";
        case TlsMode::ExistingCert:
            return "Existing cert";
    }
    return "Unknown";
}

}  // namespace

struct InstallerApp::Impl {
    explicit Impl(SystemInfo detected_system,
                  Manifest detected_manifest,
                  std::string detected_manifest_source,
                  std::string detected_manifest_error)
        : system_info(std::move(detected_system)),
          manifest(std::move(detected_manifest)),
          manifest_source(std::move(detected_manifest_source)),
          manifest_error(std::move(detected_manifest_error)),
          runner(system_info, manifest) {}

    int Run() {
        BuildComponents();
        RefreshPreflight();
        screen.Loop(root);
        return 0;
    }

    void BuildComponents() {
        tls_entries = {"Let's Encrypt", "Self-signed", "Existing cert"};
        tls_index = static_cast<int>(config.tls_mode);

        welcome_buttons = Container::Horizontal({
            Button("Re-run preflight", [&] { RefreshPreflight(); }),
            Button("Configure", [&] {
                RefreshPreflight();
                active_tab = 1;
            }),
        });

        domain_input = Input(&config.domain, "chat.example.com");
        listen_input = Input(&config.listen_address, "0.0.0.0");
        port_input = Input(&config.port_text, "6697");
        install_path_input = Input(&config.install_path, "/opt/ircord");
        data_path_input = Input(&config.data_path, "/var/lib/ircord");
        cert_input = Input(&config.existing_cert_path, "/etc/ircord/cert.pem");
        key_input = Input(&config.existing_key_path, "/etc/ircord/key.pem");
        tls_selector = Radiobox(&tls_entries, &tls_index);
        firewall_checkbox = Checkbox("Manage firewall with UFW", &config.manage_firewall);
        install_ufw_checkbox = Checkbox("Install UFW if missing", &config.install_ufw_if_missing);
        start_checkbox = Checkbox("Start after install", &config.start_after_install);

        configure_buttons = Container::Horizontal({
            Button("Back", [&] { active_tab = 0; }),
            Button("Review", [&] {
                SyncTlsMode();
                RefreshPreflight();
                active_tab = 2;
            }),
        });

        configure_form = Container::Vertical({
            domain_input,
            listen_input,
            port_input,
            install_path_input,
            data_path_input,
            tls_selector,
            cert_input,
            key_input,
            firewall_checkbox,
            install_ufw_checkbox,
            start_checkbox,
            configure_buttons,
        });

        confirm_buttons = Container::Horizontal({
            Button("Back", [&] { active_tab = 1; }),
            Button("Install", [&] {
                SyncTlsMode();
                RefreshPreflight();
                bool blocked = false;
                for (const auto& issue : preflight_issues) {
                    if (issue.blocking) {
                        blocked = true;
                        break;
                    }
                }
                if (!blocked) {
                    StartInstall();
                }
            }),
        });

        progress_buttons = Container::Horizontal({
            Button("Close", [&] {
                if (install_done) {
                    screen.ExitLoopClosure()();
                }
            }),
        });

        result_buttons = Container::Horizontal({
            Button("Exit", [&] { screen.ExitLoopClosure()(); }),
        });

        welcome_renderer = Renderer(welcome_buttons, [&] { return RenderWelcome(); });
        configure_renderer = Renderer(configure_form, [&] { return RenderConfigure(); });
        confirm_renderer = Renderer(confirm_buttons, [&] { return RenderConfirm(); });
        progress_renderer = Renderer(progress_buttons, [&] { return RenderProgress(); });
        result_renderer = Renderer(result_buttons, [&] { return RenderResult(); });

        tabs = Container::Tab({
                                  welcome_renderer,
                                  configure_renderer,
                                  confirm_renderer,
                                  progress_renderer,
                                  result_renderer,
                              },
                              &active_tab);

        root = Renderer(tabs, [&] {
            return window(
                       BoxTitle("IRCord Installer"),
                       tabs->Render() | flex) |
                   size(WIDTH, GREATER_THAN, 80) |
                   size(HEIGHT, GREATER_THAN, 28);
        });
    }

    void SyncTlsMode() {
        config.tls_mode = static_cast<TlsMode>(tls_index);
    }

    void RefreshPreflight() {
        SyncTlsMode();
        preflight_issues = runner.RunPreflight(config);
    }

    void PushLog(const std::string& line) {
        {
            std::lock_guard<std::mutex> lock(log_mutex);
            progress_lines.push_back(line);
        }
        screen.PostEvent(Event::Custom);
    }

    void StartInstall() {
        if (install_thread.joinable()) {
            install_thread.join();
        }

        install_done = false;
        install_summary = {};
        {
            std::lock_guard<std::mutex> lock(log_mutex);
            progress_lines.clear();
        }

        active_tab = 3;
        install_thread = std::thread([this] {
            install_summary = runner.RunInstall(config, [this](const std::string& step, bool is_error) {
                PushLog((is_error ? "[error] " : "[step] ") + step);
            });
            install_done = true;
            active_tab = 4;
            screen.PostEvent(Event::Custom);
        });
    }

    Element RenderWelcome() const {
        std::vector<std::string> summary = {
            "Install IRCord server on this machine.",
            "",
            "Manifest source: " + manifest_source,
            "Manifest version: " + manifest.version,
            "Docs: " + manifest.docs_url,
            "Detected platform: " + system_info.platform_key,
            "Distribution: " + (system_info.distro_id.empty() ? "unknown" : system_info.distro_id + " " + system_info.distro_version),
            "systemd: " + std::string(system_info.has_systemd ? "yes" : "no"),
            "ufw installed: " + std::string(system_info.has_ufw ? "yes" : "no"),
            "",
            "Current preflight state:",
        };

        if (!manifest_error.empty()) {
            summary.push_back("Manifest warning: " + manifest_error);
        }

        return vbox({
                   ParagraphLines(summary),
                   separator(),
                   ParagraphLines(FormatPreflight(preflight_issues)),
                   separator(),
                   welcome_buttons->Render(),
               }) |
               border;
    }

    Element RenderConfigure() const {
        Elements rows = {
            hbox(text("Domain:            "), domain_input->Render()),
            hbox(text("Listen address:    "), listen_input->Render()),
            hbox(text("Port:              "), port_input->Render()),
            hbox(text("Install path:      "), install_path_input->Render()),
            hbox(text("Data path:         "), data_path_input->Render()),
            text("TLS mode:"),
            tls_selector->Render(),
        };

        if (static_cast<TlsMode>(tls_index) == TlsMode::ExistingCert) {
            rows.push_back(hbox(text("Existing cert:     "), cert_input->Render()));
            rows.push_back(hbox(text("Existing key:      "), key_input->Render()));
        }

        rows.push_back(firewall_checkbox->Render());
        if (!system_info.has_ufw) {
            rows.push_back(install_ufw_checkbox->Render());
        }
        rows.push_back(start_checkbox->Render());
        rows.push_back(separator());
        rows.push_back(configure_buttons->Render());

        return vbox(std::move(rows)) | border;
    }

    Element RenderConfirm() const {
        std::vector<std::string> summary = {
            "Review installation settings:",
            "",
            "Domain: " + config.domain,
            "Listen address: " + config.listen_address,
            "Port: " + config.port_text,
            "Install path: " + config.install_path,
            "Data path: " + config.data_path,
            "TLS: " + TlsLabel(config.tls_mode),
            "Manage firewall: " + std::string(config.manage_firewall ? "yes" : "no"),
            "Start after install: " + std::string(config.start_after_install ? "yes" : "no"),
            "",
            "Preflight:",
        };

        return vbox({
                   ParagraphLines(summary),
                   separator(),
                   ParagraphLines(FormatPreflight(preflight_issues)),
                   separator(),
                   confirm_buttons->Render(),
               }) |
               border;
    }

    Element RenderProgress() const {
        Elements lines;
        {
            std::lock_guard<std::mutex> lock(log_mutex);
            for (const auto& line : progress_lines) {
                lines.push_back(text(line));
            }
        }
        if (lines.empty()) {
            lines.push_back(text("Waiting for installer pipeline..."));
        }

        lines.push_back(separator());
        lines.push_back(text(install_done ? "Installation finished." : "Installing..."));
        lines.push_back(progress_buttons->Render());

        return vbox(std::move(lines)) | border;
    }

    Element RenderResult() const {
        std::vector<std::string> lines = {
            install_summary.headline.empty() ? "Installation finished" : install_summary.headline,
            "",
        };
        for (const auto& detail : install_summary.details) {
            lines.push_back(detail);
        }
        if (!install_summary.docs_url.empty()) {
            lines.push_back("Docs: " + install_summary.docs_url);
        }

        return vbox({
                   ParagraphLines(lines),
                   separator(),
                   result_buttons->Render(),
               }) |
               border;
    }

    ScreenInteractive screen = ScreenInteractive::Fullscreen();
    SystemInfo system_info;
    Manifest manifest;
    std::string manifest_source;
    std::string manifest_error;
    InstallConfig config;
    InstallerRunner runner;

    std::vector<PreflightIssue> preflight_issues;
    std::vector<std::string> progress_lines;
    InstallSummary install_summary;

    int active_tab = 0;
    int tls_index = 0;
    std::vector<std::string> tls_entries;

    bool install_done = false;
    std::thread install_thread;
    mutable std::mutex log_mutex;

    Component root;
    Component tabs;
    Component welcome_buttons;
    Component configure_form;
    Component configure_buttons;
    Component confirm_buttons;
    Component progress_buttons;
    Component result_buttons;
    Component domain_input;
    Component listen_input;
    Component port_input;
    Component install_path_input;
    Component data_path_input;
    Component cert_input;
    Component key_input;
    Component tls_selector;
    Component firewall_checkbox;
    Component install_ufw_checkbox;
    Component start_checkbox;
    Component welcome_renderer;
    Component configure_renderer;
    Component confirm_renderer;
    Component progress_renderer;
    Component result_renderer;
};

InstallerApp::InstallerApp(SystemInfo system_info,
                           Manifest manifest,
                           std::string manifest_source,
                           std::string manifest_error)
    : impl_(std::make_unique<Impl>(std::move(system_info),
                                   std::move(manifest),
                                   std::move(manifest_source),
                                   std::move(manifest_error))) {}

InstallerApp::~InstallerApp() {
    if (impl_ != nullptr && impl_->install_thread.joinable()) {
        impl_->install_thread.join();
    }
}

int InstallerApp::Run() {
    return impl_->Run();
}

}  // namespace ircord::installer
