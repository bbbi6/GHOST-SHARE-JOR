# GHOST SHARE JOR - Design System

Yoyu Architectural Pixel UI — Dual Theme (**Light Drafting Paper** + **Dark Obsidian Blueprint**), applied 1:1 across both the Windows Host App (`host-app/src/main.cpp` + `host-app/src/theme.h`) and the Web Viewer (`server/www/index.html` + `server/www/tokens.css`).

## 1. Locked Color Palette & Tokens

| Token | Light Drafting Paper | Dark Obsidian Blueprint | Role |
|---|---|---|---|
| `--paper-bg` | `#F7F8FA` | `#0E0F12` | Drafting canvas background |
| `--grid-line` | `#E4E7EE` | `#181B22` | 16px graph-paper grid hairlines |
| `--grid-major` | `#CBD1DF` | `#2B303C` | 64px major drafting crosshairs / lines |
| `--surface` | `#FFFFFF` | `#16181F` | Elevated architectural tiles, cards, controls |
| `--surface-muted` | `#EFF1F5` | `#111318` | Recessed wells, URL input, segmented rail |
| `--ink` | `#111111` | `#F2F4F8` | Primary typography, active solid fill |
| `--ink-on-active` | `#FFFFFF` | `#0E0F12` | Inverted text on active solid ink blocks |
| `--ink-muted` | `#5E636E` | `#9DA4B4` | Secondary labels, room metadata, descriptions |
| `--ink-subtle` | `#858890` | `#6E7585` | Captions, inactive switches, stat labels |
| `--border` | `#D4D8E2` | `#272B36` | 1px standard architectural dividers |
| `--border-strong` | `#111111` | `#F2F4F8` | Primary frames, hovered borders, HUD borders |
| `--shadow-ink` | `#111111` | `#3B4252` | Hard pixel offset shadows (`2–6px`, zero blur) |
| `--accent` | `#FF4130` | `#FF4B3A` | Vermilion — prism base, LIVE indicator, Stop CTA, error `X_X` |
| `--mint` | `#00B894` | `#00D2A0` | Healthy telemetry, audio meter, active audio switches |
| `--cyan` | `#0EA5E9` | `#38BDF8` | Handshake / connecting indicator |

## 2. Signature Architectural Motifs

- **Drafting Graph-Paper Stage**: `16px` grid lines with `64px` major ticks/crosshairs behind the hero mascot stage.
- **Viewfinder Pixel Ghost Mascot**: 16×16 crisp pixel ghost framed by 4 sharp corner brackets `[ ]` standing above a Vermilion prism — cursor-tracking eyes, natural blink, happy `^ ^` + mint blush + live bar meter when streaming, and `X_X` vermilion eyes on error/disconnect. Zero floating ambient clutter.
- **Header Utility Pair**: Compact Moon/Sun Dark Mode toggle placed directly beside the `MOTION / STATIC` switch (`T` shortcut toggles theme on both desktop and web).
- **Direct Segmented Controls**: Zero blind-cycling buttons; direct pickers for Resolution (`SOURCE | 1440p | 1080p | 720p`), FPS (`60 HZ | 30 HZ`), Bitrate Presets (`8M | 12M | 16M | 25M | 40M`), and 20-segment draggable Pixel Bitrate Fader.

## 3. Typography & Radii

- **Sans**: `Yu Gothic UI` (fallback `Segoe UI Variable Display`, `Segoe UI`)
- **Mono**: `IBM Plex Mono` (fallback `JetBrains Mono`, `Cascadia Mono`, `Consolas`) with `tabular-nums`
- **Radii**: Strictly `0px` architectural corners across all surfaces (`DWMWCP_DONOTROUND` on Windows 11 host window)
- **Motion**: `160ms` exponential ease-out tactile hover lift + hard offset shadow, respecting `prefers-reduced-motion` and the in-app `MOTION / STATIC` toggle.
