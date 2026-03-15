# IRCord Installer

Linux-first stage-2 installer for IRCord server. It provides a terminal wizard built with FTXUI and installs a prebuilt `ircord-server` binary instead of compiling from source.

## Current scope

- Debian 12+ / Ubuntu 22.04+
- `x64` and `arm64`
- `systemd` service install
- TLS via Let's Encrypt standalone, self-signed certs, or existing cert/key
- Optional `ufw` installation and firewall rule management

## CLI

```bash
./ircord-installer --manifest-url https://chat.rausku.com/downloads/installer-manifest.json
```

For local testing you can also point at a local manifest file:

```bash
./ircord-installer --manifest-file ./installer-manifest.example.json
```

## Manifest shape

See [`assets/installer-manifest.example.json`](./assets/installer-manifest.example.json).

The installer expects:

- `version`
- `docs_url`
- `platforms.linux-x64.server.url`
- `platforms.linux-x64.server.sha256`
- `platforms.linux-arm64.server.url`
- `platforms.linux-arm64.server.sha256`

Bootstrap download URLs for the installer binary itself are deterministic:

- `https://chat.rausku.com/downloads/ircord-installer-linux-x64`
- `https://chat.rausku.com/downloads/ircord-installer-linux-arm64`

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
```
