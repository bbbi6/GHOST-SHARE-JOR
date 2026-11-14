# GHOST SHARE JOR — Design System

Hallmark · genre: `atmospheric` · macrostructure: `Workbench` · theme: `lumen` · drop: `night`

## 1. Visual Thesis — Broadcast Instrument

GHOST SHARE JOR is a zero-bloat WebRTC screen-sharing instrument across two surfaces:
1. **Web Viewer** (`server/www/index.html`) — low-latency WHEP broadcast receiver.
2. **Windows Host Rack** (`host-app/src/main.cpp`) — Direct2D WHIP broadcast transmitter.

Both surfaces share the **Lumen · Night Foundry** design system: deep dark paper surfaces, 1px hairline rules, molten brass for live signal/focus, coral chord for errors/accent words, and a two-register typographic voice where prose is strictly lowercase and machine telemetry is strictly UPPERCASE monospace with tabular figures.

---

## 2. Color Tokens (OKLCH + sRGB Equivalents)

| Token | OKLCH | sRGB Hex | Role |
|---|---|---|---|
| `--color-paper` | `oklch(13% 0.014 265)` | `#0a0a10` | Primary canvas / viewport |
| `--color-paper-2` | `oklch(17% 0.016 265)` | `#12121b` | Dock, rack surface, card fill |
| `--color-paper-3` | `oklch(21% 0.018 265)` | `#1a1a26` | Interactive control well, hover |
| `--color-ink` | `oklch(96% 0.006 262)` | `#f1f1f5` | Primary ink & headings |
| `--color-ink-2` | `oklch(78% 0.01 262)` | `#b6b7c0` | Secondary body & values |
| `--color-muted` | `oklch(62% 0.01 262)` | `#858691` | Labels, inactive ticks, hints |
| `--color-rule` | `oklch(96% 0.006 262 / 0.08)` | `rgba(241, 241, 245, 0.08)` | 1px structural hairline |
| `--color-rule-2` | `oklch(96% 0.006 262 / 0.16)` | `rgba(241, 241, 245, 0.16)` | Active/emphasized 1px rule |
| `--color-accent` | `oklch(76% 0.17 50)` | `#f29c38` | Molten brass — live signal, active states, focus |
| `--color-accent-2` | `oklch(68% 0.16 18)` | `#eb675e` | Coral chord — error state, emphasized verb |
| `--color-glow` | `oklch(80% 0.16 50 / 0.42)` | `rgba(242, 156, 56, 0.42)` | Subtle emitter dot halo only |
| `--color-paper-emit` | `oklch(76% 0.17 50 / 0.04)` | `rgba(242, 156, 56, 0.04)` | Faint brass wash on active well |
| `--rule-blueprint` | `oklch(96% 0.006 262 / 0.04)` | `rgba(241, 241, 245, 0.04)` | Blueprint grid lines |

---

## 3. Typography — Two-Register Voice

- **Display**: `"Instrument Serif", ui-serif, Georgia, serif` — weight `400`, `font-style: normal` (no italics anywhere).
- **Body / UI**: `"Geist", -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif` (Win32 fallback: `Segoe UI Variable` / `Segoe UI`).
- **Telemetry / Mono**: `"JetBrains Mono", Consolas, monospace` (Win32 fallback: `Cascadia Mono` / `Consolas`), `font-variant-numeric: tabular-nums`.

### Register Rules
1. **Prose register** (`body`, headings, buttons, links, controls): **strictly lowercase** (`text-transform: lowercase` on web; literal lowercase strings on Win32).
2. **Telemetry register** (status tags, room IDs, `1920X1080 · 60 FPS`, `16 MBPS`, `RTT 12 MS`, `PID 4812`): **strictly UPPERCASE** (`text-transform: uppercase`), tracked `0.08em`.

---

## 4. Layout, Spacing & Motion

- **4pt spacing scale**: `--space-1` (4px) through `--space-8` (32px).
- **Zero glassmorphism**: Solid `paper` / `paper-2` surfaces with `1px solid var(--color-rule)`. Never use `backdrop-filter: blur()`.
- **Corner discipline**: `2px` (`--radius-sm`) on controls and badges, `4px` (`--radius-md`) on outer cards/docks. No pill (`9999px`) buttons except tiny `6px` status emitter dots.
- **Motion**: `--ease-out: cubic-bezier(0.16, 1, 0.3, 1)`, `--dur-fast: 140ms`, `--dur-med: 260ms`. Respect `prefers-reduced-motion: reduce`.

---

## 5. Signature Components

1. **Signal Chain Apparatus** (`capture -> encode -> whip -> room`):
   - Hand-built hairline schematic diagram (SVG/CSS on web, Direct2D geometry on Windows) with live leader-line callouts carrying real session values (`ROOM`, `RETRY`, `ENDPOINT`, `NVENC`, `RES`).
2. **Real-Data Meter Strip**:
   - Vertical 1px ticks (`64` on desktop, `32` on mobile) whose heights render the live `pc.getStats()` bitrate envelope rather than decorative bars.
3. **Tick Slider (Windows Host)**:
   - Interactive 2–60 Mbps stepped tick ruler supporting pointer drag, `WM_MOUSEWHEEL`, and Left/Right arrow keys.

---

## 6. Anti-Slop Guardrails

- No `backdrop-filter: blur`
- No glowing orb / CSS spinner rings
- No italic serif headings
- No gradient text or multi-hue neon glows
- No emoji icons
- No two-cell stat card grids
- No fake browser chrome
