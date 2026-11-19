#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <stdint.h>
#include <string>
#include <initializer_list>
#include <windows.h>

// Locked Architectural Pixel Design Tokens (Yoyu Design DNA)
// Synced 1:1 with server/www/tokens.css and server/www/index.html
namespace StudioTheme {

struct Palette {
    uint32_t paper_bg;
    uint32_t grid_line;
    uint32_t grid_major;
    uint32_t surface;
    uint32_t surface_muted;
    uint32_t ink;
    uint32_t ink_on_active;
    uint32_t ink_muted;
    uint32_t ink_subtle;
    uint32_t border;
    uint32_t border_strong;
    uint32_t shadow_ink;
    uint32_t ghost_outline;
    uint32_t ghost_fill;
    uint32_t ghost_eyes;
    uint32_t ghost_shadow;
    uint32_t accent_vermilion;
    uint32_t pixel_mint;
    uint32_t pixel_cyan;
};

constexpr Palette PALETTE_LIGHT = {
    0xF7F8FA, // paper_bg
    0xE4E7EE, // grid_line
    0xCBD1DF, // grid_major
    0xFFFFFF, // surface
    0xEFF1F5, // surface_muted
    0x111111, // ink
    0xFFFFFF, // ink_on_active
    0x5E636E, // ink_muted
    0x858890, // ink_subtle
    0xD4D8E2, // border
    0x111111, // border_strong
    0x111111, // shadow_ink
    0x111111, // ghost_outline
    0xFFFFFF, // ghost_fill
    0x111111, // ghost_eyes
    0xCBD1DF, // ghost_shadow
    0xFF4130, // accent_vermilion
    0x00B894, // pixel_mint
    0x0EA5E9  // pixel_cyan
};

constexpr Palette PALETTE_DARK = {
    0x0E0F12, // paper_bg (Obsidian drafting surface)
    0x181B22, // grid_line (16px blueprint grid)
    0x2B303C, // grid_major (64px crosshairs)
    0x16181F, // surface (elevated tile)
    0x111318, // surface_muted (recessed tile)
    0xF2F4F8, // ink
    0x0E0F12, // ink_on_active
    0x9DA4B4, // ink_muted
    0x6E7585, // ink_subtle
    0x272B36, // border
    0xF2F4F8, // border_strong
    0x3B4252, // shadow_ink
    0xF2F4F8, // ghost_outline
    0xF2F4F8, // ghost_fill
    0x0E0F12, // ghost_eyes
    0x060709, // ghost_shadow
    0xFF4B3A, // accent_vermilion
    0x00D2A0, // pixel_mint
    0x38BDF8  // pixel_cyan
};

inline const Palette& palette(bool dark) {
    return dark ? PALETTE_DARK : PALETTE_LIGHT;
}

inline D2D1_COLOR_F color(uint32_t rgb, float alpha = 1.0f) {
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8) & 0xFF) / 255.0f,
        (rgb & 0xFF) / 255.0f,
        alpha);
}

struct FontSet {
    std::wstring bodyFamily = L"Yu Gothic UI";
    std::wstring monoFamily = L"IBM Plex Mono";
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
    fs.bodyFamily = pick_system_font(dw, {L"Yu Gothic UI", L"Segoe UI Variable Display", L"Segoe UI"}, L"Segoe UI");
    fs.monoFamily = pick_system_font(dw, {L"IBM Plex Mono", L"JetBrains Mono", L"Cascadia Mono", L"Consolas"}, L"Consolas");
    return fs;
}

} // namespace StudioTheme
