# GHOST SHARE JOR

Ultra-low latency, high-clarity screen sharing with system audio and Discord voice isolation.

Host: Native Windows Desktop App (`GhostShareJor.exe`)  
Viewers: Web Browser Room Link (Zero Install, Desktop & Mobile iOS/Android)

---

## Overview

GHOST SHARE JOR is a self-hosted screen sharing solution designed for friends, gamers, developers, and teams who require high-resolution (up to 4K / 60 FPS) and low latency (~0.2s delay) without paying subscription fees or installing third-party viewer software.

Viewers join via a room URL in any modern browser without downloading plugins or applications. The Windows host application captures screen frames directly via Direct3D 11 GPU APIs, routes audio with process-level isolation, encodes via hardware acceleration, and pushes video to a MediaMTX WebRTC SFU over WHIP.

---

## Key Features

- Direct3D 11 GPU Capture: Hardware-accelerated desktop capture with zero CPU copy overhead. Supports native multi-monitor setups, high-refresh displays, and HDR/SDR pipelines.
- Multi-Resolution Selector:
  - Source (Native): Unscaled display resolution (e.g. 4K, 1440p, ultrawide).
  - 1440p (QHD): 2560x1440 GPU scaling.
  - 1080p (FHD): 1920x1080 standard high-definition.
  - 720p (HD): 1280x720 bandwidth saver.
- Framerate Control: 60 FPS (smooth motion for gaming/video) and 30 FPS (bandwidth optimization for code and desktop productivity).
- Custom Bitrate Controls: Dedicated quick-chips (8 Mbps, 12 Mbps, 16 Mbps, 25 Mbps, 40 Mbps) plus fine-tuning steppers (2 to 60 Mbps).
- Discord Voice Isolation: Uses Windows Core Audio WASAPI 2 Process Tree Exclusion (`exclude-process-tree`). Automatically discovers Discord's process tree and excludes voice chat from the stream. Viewers hear all game and system audio without hearing their own voices looped back as echo.
- WebRTC Low Latency Pipeline: Sub-second delay (~0.2s glass-to-glass) via WHIP ingest and WHEP distribution over UDP.
- Zero-Install Browser Viewer: Modern dark-themed viewer page compatible with Google Chrome, Mozilla Firefox, Microsoft Edge, Safari on iOS/iPadOS, and Android browsers.
- Lightweight Native Host: Written in modern C++ with Win32 and Direct2D / DirectWrite hardware-rendered HUD. Standalone portable binary (~270 KB) with no Electron or Chromium runtime overhead.
- Automated Server Stack: Docker Compose deployment using MediaMTX and Caddy with automatic Let's Encrypt HTTPS certificate provisioning.

---

## System Architecture

```text
+-------------------------------------------------------------------------+
|                            Windows Host PC                              |
|                                                                         |
|  [ Direct3D 11 Capture ] ---> [ GPU NV12 Format Conversion ]            |
|                                         |                               |
|  [ WASAPI Loopback Audio ]              v                               |
|   (with Discord Excluded) ---> [ Hardware Encoder: NVENC / MFH264 ]    |
|                                         |                               |
|                                         v                               |
|                              [ WebRTC WHIP Publisher ]                  |
+-------------------------------------------------------------------------+
                                          |
                                          | WHIP (H.264 / Opus) ~10-16 Mbps
                                          v
+-------------------------------------------------------------------------+
|                       Media Server (Ubuntu 24.04)                       |
|                                                                         |
|  [ Caddy Reverse Proxy ]   - Port 80, 443 (Auto SSL & Web Static UI)   |
|  [ MediaMTX WebRTC SFU ]  - Port 8189 UDP/TCP (ICE Data Engine)        |
+-------------------------------------------------------------------------+
       |                                   |                           |
       | WHEP (~0.2s delay)                | WHEP                      | WHEP
       v                                   v                           v
[ Viewer 1: Chrome ]              [ Viewer 2: iOS Safari ]    [ Viewer 3: Android ]
```

---

## Directory Structure

```text
ghost-share-jor/
├── host-app/                   # Windows native host application source
│   ├── src/
│   │   ├── main.cpp            # Win32 + Direct2D UI, settings, and process manager
│   │   ├── ghost_share_core.h  # GStreamer pipeline builder & Discord PID finder
│   │   ├── ghost.rc            # Application resource script
│   │   └── ghost.res           # Compiled icon resource
│   ├── assets/
│   │   └── ghost.ico           # Application icon
│   └── build.bat               # MSVC 64-bit compilation script
│
├── dist/                       # Release folder
│   └── GhostShareJor/
│       ├── GhostShareJor.exe   # Portable executable (~270 KB)
│       └── server.txt          # Server address configuration file
│
├── server/                     # Media server configuration
│   ├── Caddyfile               # Caddy reverse proxy configuration
│   ├── docker-compose.yml      # MediaMTX + Caddy container definitions
│   ├── mediamtx.yml            # MediaMTX WebRTC SFU configuration
│   ├── setup-server.sh         # Interactive deployment script
│   └── www/
│       └── index.html          # Browser viewer web application
│
├── .gitignore
├── README.md
└── UBUNTU_24_04_SETUP.md       # Complete Ubuntu deployment manual
```

---

## Server Deployment (Ubuntu 24.04 LTS)

### Prerequisites
- Ubuntu 24.04 LTS VPS with a public IP.
- Recommended: 2 to 4 GB RAM, 2 or more CPU cores.
- Open firewall ports:
  - 80/TCP (HTTP & Let's Encrypt challenge)
  - 443/TCP (HTTPS & WHEP signaling)
  - 8189/UDP and 8189/TCP (WebRTC ICE media data)

### Quick Start
1. Transfer the `server` directory to your server:
   ```bash
   scp -r server user@your-server-ip:~/ghost-server
   ```
2. Connect via SSH and run the setup script:
   ```bash
   ssh user@your-server-ip
   cd ~/ghost-server
   sudo bash setup-server.sh
   ```
3. Enter your domain name (e.g. `share.yourdomain.com`). The script will install Docker, open firewall ports, configure SSL, and start the containers.

Detailed instructions are available in [UBUNTU_24_04_SETUP.md](UBUNTU_24_04_SETUP.md).

---

## Host Application Setup (Windows)

### Running the Pre-built Binary
1. Open `dist/GhostShareJor/GhostShareJor.exe`.
2. Click the **SERVER URL** card and paste your server URL (e.g. `https://share.yourdomain.com`).
3. Select your desired Resolution, Framerate, and Bitrate.
4. Toggle **EXCLUDE DISCORD** (ON by default) to filter out Discord voice chat.
5. Click **START SHARING SCREEN**.
6. Click **COPY LINK** and send the room URL to viewers.

### Building from Source
#### Requirements:
- Microsoft Visual Studio 2022 Build Tools (MSVC with C++17 support).
- Windows 10 SDK (for Direct2D, DirectWrite, and Core Audio headers).
- GStreamer 1.24+ binaries (e.g., MSYS2 UCRT64 `mingw-w64-ucrt-x86_64-gstreamer`).

#### Build Command:
Open a command prompt in `host-app`:
```bat
cd host-app
build.bat
```
The compiled binary will be placed in `dist/GhostShareJor/GhostShareJor.exe`.

---

## Discord Voice Isolation Details

Screen sharing often suffers from voice echo when participants on Discord hear their own voices repeated through the stream's loopback capture.

GHOST SHARE JOR eliminates this issue via native Windows Core Audio APIs:
1. `find_discord_root_pid()` inspects running processes via `CreateToolhelp32Snapshot` to identify the root process of `Discord.exe` (as well as Discord Canary and Discord PTB).
2. The GStreamer `wasapi2src` element is configured with `loopback=true`, `loopback-mode=exclude-process-tree`, and `loopback-target-pid=<PID>`.
3. Windows excludes the target process and all child audio threads from the loopback buffer at the kernel audio driver level.
4. Result: Viewers hear all game, music, and system audio without hearing any voice feedback from Discord.

---

## Viewer Experience

- No plugins or applications required.
- Opens instantly on iOS (Safari), Android (Chrome), Windows, macOS, and Linux.
- WebRTC adaptive jitter buffer maintains minimum latency (~0.2s).
- Fullscreen and audio unmute controls with responsive layouts for mobile and desktop screens.

---

## License

MIT License. See individual source files for additional copyright notices.
