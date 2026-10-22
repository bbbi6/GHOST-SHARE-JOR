# Installation Guide: GHOST SHARE JOR Server on Ubuntu 24.04 LTS

Low-Latency WebRTC SFU Server (MediaMTX + Caddy Auto-HTTPS)
Supports Windows desktop host (`GhostShareJor.exe`) and web-based viewers (desktop and mobile browsers).

---

## 1. Server Requirements & Firewall Ports

### Hardware Specifications
- **Operating System**: Ubuntu 24.04 LTS (Noble Numbat)
- **CPU**: 2 - 6 Cores recommended (SFU only forwards packets without re-encoding, resulting in low CPU usage)
- **RAM**: 2 - 4 GB
- **Network Uplink**: 100 Mbps or higher recommended (low latency routing ensures stream delay under 0.3s)

### Firewall Ports
| Port | Protocol | Purpose |
| :--- | :--- | :--- |
| **80** | TCP | HTTP challenge & automatic SSL certificate generation |
| **443** | TCP | HTTPS for viewer web interface & WebRTC WHEP signaling |
| **8189** | UDP & TCP | WebRTC ICE Media data transmission (Mandatory) |

> [!WARNING]
> If using **Cloudflare DNS**: In DNS Management, set Proxy Status to **DNS only (grey cloud)**. Cloudflare free tier does not route UDP traffic on port 8189.

---

## 2. Uploading Server Files to VPS

Open PowerShell on your Windows workstation and copy the `server` directory to your server:

```powershell
# Example: replace your-user and your-vps-ip with your actual credentials
scp -r "C:\Users\ADMIN\.gemini\antigravity\scratch\ghost-share-jor\server" your-user@your-vps-ip:~/ghost-server
```

Connect via SSH:
```bash
ssh your-user@your-vps-ip
```

---

## 3. Automated Installation (Recommended)

Run the automated setup script:

```bash
cd ~/ghost-server
sudo bash setup-server.sh
```

The script will prompt for your domain name or IP:
```text
[?] Enter your domain (e.g., share.mydomain.com) or Public IP:
```
- **Domain (Recommended)**: Enter your full domain, e.g. `share.mydomain.com` (Caddy automatically requests and renews Let's Encrypt SSL certificates).
- **Public IP**: Enter server public IP if running on raw IP without HTTPS.

The script installs Docker, Docker Compose, configures UFW firewall rules, and starts all containers automatically.

---

## 4. Manual Installation (Step-by-step)

If you prefer to configure each component manually:

### Step 4.1: Update System & Install Docker
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y ca-certificates curl ufw

curl -fsSL https://get.docker.com | sudo sh
sudo systemctl enable --now docker
```

### Step 4.2: Configure Firewall (UFW)
```bash
sudo ufw allow 22/tcp     # SSH access
sudo ufw allow 80/tcp     # HTTP
sudo ufw allow 443/tcp    # HTTPS
sudo ufw allow 8189/udp   # WebRTC ICE Media
sudo ufw allow 8189/tcp   # WebRTC ICE Fallback
sudo ufw enable
```

### Step 4.3: Configure Domain in Caddyfile & MediaMTX
Navigate to the server directory:
```bash
cd ~/ghost-server
```

#### Edit `Caddyfile`:
```bash
nano Caddyfile
```
Change line 2 from `:80` to your actual domain name:
```caddy
share.mydomain.com {
    root * /srv/www
    ...
```

#### Edit `mediamtx.yml`:
```bash
nano mediamtx.yml
```
Locate `webrtcAdditionalHosts` and specify your public domain or IP:
```yaml
webrtcAdditionalHosts: [ "share.mydomain.com" ]
```

### Step 4.4: Start Containers
```bash
sudo docker compose down
sudo docker compose up -d
```

---

## 5. Maintenance & Useful Commands

### Check Container Status
```bash
sudo docker compose ps
```

### View Live Logs
```bash
sudo docker compose logs -f
```

### Check Active Ports
```bash
sudo ss -tulpn | grep -E '80|443|8189|8889'
```

### Restart Server
```bash
sudo docker compose restart
```

### Stop Server
```bash
sudo docker compose down
```

---

## 6. Verification and Testing

1. **Viewer Webpage**: Open a browser on desktop or mobile:
   ```
   https://share.mydomain.com
   ```
   The dark zinc interface will load and display connection status.

2. **Host App (Windows)**:
   - Run `GhostShareJor.exe`.
   - Click the **SERVER URL** card and paste `https://share.mydomain.com`.
   - Click **START SHARING SCREEN**.
   - When status shows **LIVE**, click **COPY LINK** and share with viewers.

3. **Viewer Experience**:
   - Viewers open `https://share.mydomain.com/r/ghost_xxx` to watch the stream with low latency (~0.2s delay).
   - Click **Tap to Unmute Audio** to hear system audio.
