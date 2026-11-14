#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <windows.h>

namespace LumenTheme {

// Lumen · Night Foundry (OKLCH -> sRGB hex equivalents)
constexpr uint32_t PAPER      = 0x0A0A10; // oklch(13% 0.014 265)
constexpr uint32_t PAPER_2    = 0x12121B; // oklch(17% 0.016 265)
constexpr uint32_t PAPER_3    = 0x1A1A26; // oklch(21% 0.018 265)
constexpr uint32_t INK        = 0xF1F1F5; // oklch(96% 0.006 262)
constexpr uint32_t INK_2      = 0xB6B7C0; // oklch(78% 0.01 262)
constexpr uint32_t MUTED      = 0x858691; // oklch(62% 0.01 262)
constexpr uint32_t ACCENT     = 0xF29C38; // oklch(76% 0.17 50) molten brass
constexpr uint32_t ACCENT_2   = 0xEB675E; // oklch(68% 0.16 18) coral chord

constexpr float RULE_ALPHA        = 0.08f;
constexpr float RULE_2_ALPHA      = 0.16f;
constexpr float BLUEPRINT_ALPHA   = 0.04f;
constexpr float PAPER_EMIT_ALPHA  = 0.06f;
constexpr float GLOW_ALPHA        = 0.35f;

inline D2D1_COLOR_F color(uint32_t rgb, float alpha = 1.0f) {
  return D2D1::ColorF(
      ((rgb >> 16) & 0xFF) / 255.0f,
      ((rgb >> 8) & 0xFF) / 255.0f,
      (rgb & 0xFF) / 255.0f,
      alpha);
}

struct FontSet {
  IDWriteFontCollection* collection = nullptr; // nullptr => system fallback
  std::wstring bodyFamily = L"Segoe UI";
  std::wstring monoFamily = L"Consolas";
  std::wstring displayFamily = L"Georgia";

  void release() {
    if (collection) {
      collection->Release();
      collection = nullptr;
    }
  }
};

// Loads optional .ttf/.otf fonts from <exeDir>\fonts\ via IDWriteFactory5.
// Falls back cleanly to standard Windows faces if the directory is absent.
inline FontSet load_fonts(IDWriteFactory* baseFactory) {
  FontSet fs;
  if (!baseFactory) return fs;

  wchar_t exePath[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  std::wstring dir(exePath);
  size_t slash = dir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) dir = dir.substr(0, slash);
  std::wstring fontsDir = dir + L"\\fonts";

  WIN32_FIND_DATAW fd = {};
  HANDLE hFind = FindFirstFileW((fontsDir + L"\\*.ttf").c_str(), &fd);
  if (hFind == INVALID_HANDLE_VALUE) {
    return fs;
  }

  std::vector<std::wstring> files;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      files.push_back(fontsDir + L"\\" + fd.cFileName);
    }
  } while (FindNextFileW(hFind, &fd));
  FindClose(hFind);

  if (files.empty()) return fs;

  IDWriteFactory5* factory5 = nullptr;
  if (FAILED(baseFactory->QueryInterface(__uuidof(IDWriteFactory5), reinterpret_cast<void**>(&factory5))) || !factory5) {
    return fs;
  }

  IDWriteFontSetBuilder1* builder = nullptr;
  if (SUCCEEDED(factory5->CreateFontSetBuilder(&builder)) && builder) {
    for (const auto& path : files) {
      IDWriteFontFile* fontFile = nullptr;
      if (SUCCEEDED(factory5->CreateFontFileReference(path.c_str(), nullptr, &fontFile)) && fontFile) {
        builder->AddFontFile(fontFile);
        fontFile->Release();
      }
    }
    IDWriteFontSet* fontSet = nullptr;
    if (SUCCEEDED(builder->CreateFontSet(&fontSet)) && fontSet) {
      IDWriteFontCollection1* col1 = nullptr;
      if (SUCCEEDED(factory5->CreateFontCollectionFromFontSet(fontSet, &col1)) && col1) {
        fs.collection = col1;
        auto has_family = [&](const wchar_t* name) -> bool {
          UINT32 idx = 0;
          BOOL exists = FALSE;
          return SUCCEEDED(col1->FindFamilyName(name, &idx, &exists)) && exists;
        };
        if (has_family(L"Geist")) fs.bodyFamily = L"Geist";
        if (has_family(L"JetBrains Mono")) fs.monoFamily = L"JetBrains Mono";
        if (has_family(L"Instrument Serif")) fs.displayFamily = L"Instrument Serif";
      }
      fontSet->Release();
    }
    builder->Release();
  }
  factory5->Release();
  return fs;
}

} // namespace LumenTheme
