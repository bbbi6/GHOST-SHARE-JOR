# GHOST SHARE JOR - Server (Ubuntu 24.04 LTS)

Documentation and deployment files for MediaMTX SFU and Caddy Reverse Proxy.

## Quick Start
```bash
sudo bash setup-server.sh
```
Enter your domain (e.g., `share.yourdomain.com`) or public IP address. The script will automatically configure Docker, firewall rules (UFW), and start the containers.

## Common Management Commands
- View live logs: `sudo docker compose logs -f`
- Restart services: `sudo docker compose restart`
- Stop services: `sudo docker compose down`
- Update and start: `sudo docker compose up -d`

## Cloudflare Notice
If routing through Cloudflare DNS, set the Proxy status to **DNS only (grey cloud)**. Cloudflare free proxy does not forward WebRTC UDP traffic on port 8189.
