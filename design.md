# GHOST SHARE JOR - Design System

Obsidian Cinema + Lime Green (applied consistently across both Web Viewer `server/www/index.html` and Windows Host App `host-app/src/main.cpp`).

## 1. Color Palette & Tokens

| Token | CSS / Host Value | Role |
|---|---|---|
| `--bg-void` | `#030305` | Primary viewport / cinema canvas with radial lime bloom |
| `--bg-panel` | `rgba(12, 14, 12, 0.78)` (`#0C0E0C` native) | Floating panels, top pills, cinema dock cards |
| `--bg-chip` | `rgba(255, 255, 255, 0.06)` | Control buttons, segmented wells, switch off-track, URL well |
| `--bg-chip-hover` | `rgba(255, 255, 255, 0.13)` | Hover fill on chips and buttons |
| `--border-hairline` | `rgba(255, 255, 255, 0.09)` | 1px subtle panel & control borders |
| `--border-strong` | `rgba(255, 255, 255, 0.16)` | Focus / hover hairline border |
| `--text-primary` | `#fbfbfb` | Primary headings, controls, active states |
| `--text-secondary` | `#a1a1aa` | Body descriptions, inactive tabs |
| `--text-muted` | `#71717a` | Hints, secondary metadata keys |
| `--accent` | `#84cc16` | Lime Green - Ghost emblem, Signal Radar, bitrate bar, switches, primary CTA |
| `--accent-light` | `#bef264` | Active chip text, slider thumb/readout, live accents |
| `--accent-soft` | `rgba(132, 204, 22, 0.18)` | Active control fill |
| `--accent-border` | `rgba(132, 204, 22, 0.45)` | Active control border |
| `--live-red` | `#ef4444` | Live crimson pill (`rgba(239, 68, 68, 0.14)` fill), Stop CTA, error states |

## 2. Signature Visual Motifs

- **Ambient Lime Bloom**: Subtle radial gradient behind top/center stage (`rgba(132, 204, 22, 0.08)` -> `transparent`).
- **Ghost Emblem**: Minimal domed `#84cc16` silhouette with `#030305` eyes inside a `9999px` brand pill.
- **Signal Radar Indicator**: Concentric radar ring with sweeping / pulsing lime signal dot for `STANDBY`, `CONNECTING`, and `LIVE` state.

## 3. Typography & Copy Rules

- **Web Sans**: `-apple-system, BlinkMacSystemFont, "Segoe UI Variable Text", "Segoe UI", system-ui, sans-serif`
- **Desktop Sans**: Auto-selected `Segoe UI Variable Text` (fallback `Segoe UI`) via DirectWrite system font lookup
- **Telemetry Mono**: `ui-monospace, "Cascadia Mono", "Consolas", monospace` (`tabular-nums`)
- **Copy voice**: Direct, concise, zero em-dashes

## 4. Radii & Architecture

- **Radii**: `9999px` full pills (brand badge, status/telemetry pills), `16px` primary cards & cinema dock, `10px` control buttons & chips (`DWMWCP_ROUND` on Windows 11 host window)
- **CSS Delivery**: Shared tokens live in `server/www/tokens.css`, with critical `:root` variables mirrored inline inside `server/www/index.html` and mapped 1:1 in `host-app/src/theme.h`
