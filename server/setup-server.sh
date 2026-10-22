#!/bin/bash
set -e

echo "=========================================="
echo "GHOST SHARE JOR - Server Setup"
echo "=========================================="

# Check root
if [ "$EUID" -ne 0 ]; then
  echo "[-] Please run as root (sudo bash setup-server.sh)"
  exit 1
fi

# Ask for domain or public IP
read -p "[?] Enter your domain (e.g., share.mydomain.com) or Public IP: " DOMAIN_OR_IP
if [ -z "$DOMAIN_OR_IP" ]; then
  echo "[-] Domain or IP cannot be empty!"
  exit 1
fi

echo "[*] Updating package list..."
apt-get update -y

# Install Docker if missing
if ! command -v docker &> /dev/null; then
  echo "[*] Installing Docker via official installer..."
  apt-get install -y ca-certificates curl
  curl -fsSL https://get.docker.com | sh
  systemctl enable --now docker
fi

# Configure Firewall (UFW)
if command -v ufw &> /dev/null; then
  echo "[*] Opening firewall ports (80/tcp, 443/tcp, 8189/udp, 8189/tcp)..."
  ufw allow 80/tcp
  ufw allow 443/tcp
  ufw allow 8189/udp
  ufw allow 8189/tcp
fi

# Update Caddyfile with domain
if [[ "$DOMAIN_OR_IP" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "[*] Using HTTP on IP address..."
  sed -i "s/:80/$DOMAIN_OR_IP:80/g" Caddyfile
else
  echo "[*] Using automatic HTTPS for domain $DOMAIN_OR_IP..."
  sed -i "s/:80/$DOMAIN_OR_IP/g" Caddyfile
fi

# Update MediaMTX with public IP / domain
sed -i "s/# webrtcAdditionalHosts:.*/webrtcAdditionalHosts: [ \"$DOMAIN_OR_IP\" ]/g" mediamtx.yml

echo "[*] Starting GHOST SHARE JOR Server containers..."
docker compose down || true
docker compose up -d

echo ""
echo "=========================================="
echo "[+] Server Setup Complete!"
echo "Viewer Page: http://$DOMAIN_OR_IP (or https://$DOMAIN_OR_IP)"
echo "WHIP Endpoint: http://$DOMAIN_OR_IP/{room}/whip"
echo "WHEP Endpoint: http://$DOMAIN_OR_IP/{room}/whep"
echo "=========================================="
