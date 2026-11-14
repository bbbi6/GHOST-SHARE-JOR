#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <stdint.h>
#include <string>
#include <initializer_list>
#include <windows.h>

namespace StudioTheme {

// Obsidian Cinema + Lime Green palette (synced 1:1 with server/www/index.html & tokens.css)
constexpr uint32_t BG_VOID      = 0x030305;
constexpr uint32_t BG_PANEL     = 0x0C0E0C;
constexpr uint32_t BG_SURFACE   = 0x101310;
constexpr uint32_t BG_WELL      = 0x080A08;

constexpr uint32_t TEXT_PRIMARY   = 0xFBFBFB;
constexpr uint32_t TEXT_SECONDARY = 0xA1A1AA;
constexpr uint32_t TEXT_MUTED     = 0x71717A;
constexpr uint32_t TEXT_DARK      = 0x030305;

constexpr uint32_t ACCENT         = 0x84CC16; // Lime Green (--accent)
constexpr uint32_t ACCENT_LIGHT   = 0xBEF264; // Soft Lime (--accent-light)
constexpr uint32_t LIVE_RED       = 0xEF4444; // Semantic Live / Stop (--live-red)
constexpr uint32_t DANGER_TEXT    = 0xFCA5A5;

constexpr float PANEL_ALPHA         = 0.88f;
constexpr float CHIP_ALPHA          = 0.06f;
constexpr float CHIP_HOVER_ALPHA    = 0.13f;
constexpr float BORDER_HAIRLINE     = 0.09f;
constexpr float BORDER_STRONG       = 0.16f;
constexpr float ACCENT_SOFT_ALPHA   = 0.18f;
constexpr float ACCENT_BORDER_ALPHA = 0.45f;
constexpr float LIVE_RED_BG_ALPHA   = 0.14f;
constexpr float LIVE_RED_BDR_ALPHA  = 0.45f;

inline D2D1_COLOR_F color(uint32_t rgb, float alpha = 1.0f) {
  return D2D1::ColorF(
      ((rgb >> 16) & 0xFF) / 255.0f,
      ((rgb >> 8) & 0xFF) / 255.0f,
      (rgb & 0xFF) / 255.0f,
      alpha);
}

struct FontSet {
  std::wstring bodyFamily = L"Segoe UI";
  std::wstring monoFamily = L"Consolas";
};

inline std::wstring pick_system_font(IDWriteFactory* dw, std::initializer_list<const wchar_t*> candidates, const wchar_t* fallback) {
  if (!dw) return fallback;
  IDWriteFontCollection* col = nullptr;
  if (FAILED(dw->GetSystemFontCollection(&col, FALSE)) || !col) return fallback;

  std::wstring picked = fallback;
  for (const wchar_t* name : candidates) {
    UINT32 idx = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(col->FindFamilyName(name, &idx, &exists)) && exists) {
      picked = name;
      break;
    }
  }
  col->Release();
  return picked;
}

inline FontSet load_fonts(IDWriteFactory* dw) {
  FontSet fs;
  fs.bodyFamily = pick_system_font(dw, {L"Segoe UI Variable Text", L"Segoe UI"}, L"Segoe UI");
  fs.monoFamily = pick_system_font(dw, {L"Cascadia Mono", L"Consolas"}, L"Consolas");
  return fs;
}

} // namespace StudioTheme
