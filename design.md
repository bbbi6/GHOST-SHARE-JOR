# GHOST SHARE JOR - Design System

Obsidian Broadcast Studio (applied consistently across both Web Viewer and Windows Host App).

## 1. Color Palette

| Token | Hex / Value | Role |
|---|---|---|
| `--bg-canvas` | `#09090b` | Primary viewport / canvas |
| `--bg-surface` | `#111114` | Cards, panels, top pills, dock |
| `--bg-elevated` | `#18181d` | Interactive controls, badges, switch off-track |
| `--bg-hover` | `#202027` | Hover fill |
| `--border-hairline` | `rgba(255, 255, 255, 0.08)` | 1px subtle panel & control borders |
| `--border-strong` | `rgba(255, 255, 255, 0.16)` | Active segment / hover border |
| `--text-primary` | `#fafafa` | Primary headings, active states |
| `--text-secondary` | `#a1a1aa` | Body descriptions, inactive tabs |
| `--text-muted` | `#71717a` | Hints, secondary metadata keys |
| `--accent` | `#84cc16` | Lime Green - primary brand accent, radar, bitrate slider, toggles, CTA |
| `--live` | `#22c55e` | Semantic Live / Discord PID isolated indicator |
| `--danger` | `#ef4444` | Semantic Stop Sharing / error state |

## 2. Typography & Copy Rules

- **Web Sans**: `"Geist", "Segoe UI Variable Text", -apple-system, BlinkMacSystemFont, "Segoe UI", system-ui, sans-serif`
- **Desktop Sans**: Auto-selected `Segoe UI Variable Text` (fallback `Segoe UI`) via DirectWrite system font lookup
- **Telemetry Mono**: `Cascadia Mono` / `JetBrains Mono` / `Consolas` (`tabular-nums`)
- **Copy voice**: Direct, concise, zero em-dashes, normal sentence/title capitalization

## 3. Radii & Motion

- **Radii**: `6-8px` for inner buttons & segments, `10-12px` for primary cards & dock (`DWMWCP_ROUND` on Windows 11 host window)
- **CSS Delivery**: All tokens are self-contained inside `server/www/index.html` (`:root`) so local `file://` opens and subpath routes render identically without external CSS dependencies
