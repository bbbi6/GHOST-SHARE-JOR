#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <stdint.h>
#include <string>
#include <initializer_list>
#include <windows.h>

namespace StudioTheme {

// Obsidian Broadcast Studio palette (synced 1:1 with server/www/index.html)
constexpr uint32_t BG_CANVAS    = 0x09090B;
constexpr uint32_t BG_SURFACE   = 0x111114;
constexpr uint32_t BG_ELEVATED  = 0x18181D;
constexpr uint32_t BG_HOVER     = 0x202027;
constexpr uint32_t BG_ACTIVE    = 0x25252E;

constexpr uint32_t TEXT_PRIMARY   = 0xFAFAFA;
constexpr uint32_t TEXT_SECONDARY = 0xA1A1AA;
constexpr uint32_t TEXT_MUTED     = 0x71717A;
constexpr uint32_t TEXT_DARK      = 0x09090B;

constexpr uint32_t ACCENT       = 0x38BDF8; // Signal Cyan
constexpr uint32_t ACCENT_HOVER = 0x67CEFA;
constexpr uint32_t LIVE_GREEN   = 0x22C55E; // Semantic Live
constexpr uint32_t DANGER_RED   = 0xEF4444; // Semantic Stop / Error
constexpr uint32_t DANGER_TEXT  = 0xFCA5A5;

constexpr float BORDER_ALPHA     = 0.08f;
constexpr float BORDER_STR_ALPHA = 0.16f;
constexpr float HIGHLIGHT_ALPHA  = 0.06f;
constexpr float ACCENT_BG_ALPHA  = 0.14f;
constexpr float DANGER_BG_ALPHA  = 0.15f;
constexpr float GREEN_BG_ALPHA   = 0.14f;

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
