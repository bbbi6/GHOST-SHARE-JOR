/* Hallmark · studied-DNA (source: https://yoyu.design/ via Awwwards MCP)
 * Ghost Share Jor Host App — Architectural Pixel UI (Light Paper + Dark Obsidian Blueprint)
 * Unified design twin with Ghost Mirror v2.0.0 and server/www/index.html
 * pre-emit critique: P5 H5 E5 S5 R5 V5
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif

#include "ghost_share_core.h"
#include "theme.h"

constexpr float LOGICAL_W = 480.0f;
constexpr float LOGICAL_H = 704.0f;

constexpr UINT_PTR TIMER_ANIM      = 1;
constexpr UINT_PTR TIMER_APPLY_CFG = 2;
constexpr UINT_PTR TIMER_CLOCK     = 3;
constexpr UINT WM_STREAM_STATE     = WM_APP + 1;

static const int BITRATE_PRESETS[5] = { 8, 12, 16, 25, 40 };

enum HitId : int {
    HIT_NONE = 0,
    HIT_CLOSE,
    HIT_MIN,
    HIT_MOTION,
    HIT_THEME,
    HIT_RES_0, HIT_RES_1, HIT_RES_2, HIT_RES_3,
    HIT_FPS_0, HIT_FPS_1,
    HIT_BIT_0, HIT_BIT_1, HIT_BIT_2, HIT_BIT_3, HIT_BIT_4,
    HIT_BITRATE_FADER,
    HIT_AUDIO,
    HIT_DISCORD,
    HIT_SERVER,
    HIT_URL_WELL,
    HIT_CLIP_PASTE,
    HIT_CLIP_DISMISS,
    HIT_COPY,
    HIT_NEW_ROOM,
    HIT_OPEN_WEB,
    HIT_START_STOP,
    HIT_COUNT
};

// ============================================================================
// Direct2D / DirectWrite / App State
// ============================================================================
static HWND g_hwnd = nullptr;
static HICON g_appIcon = nullptr;
static HICON g_appIconSmall = nullptr;
static ID2D1Factory* g_d2d = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static ID2D1SolidColorBrush* g_br = nullptr; // Single cached brush: zero per-frame COM allocs!
static IDWriteFactory* g_dw = nullptr;
static StudioTheme::FontSet g_fonts;

static IDWriteTextFormat* g_fmtBrand     = nullptr;
static IDWriteTextFormat* g_fmtTag       = nullptr;
static IDWriteTextFormat* g_fmtTitle     = nullptr;
static IDWriteTextFormat* g_fmtSub       = nullptr;
static IDWriteTextFormat* g_fmtSeg       = nullptr;
static IDWriteTextFormat* g_fmtMicro     = nullptr;
static IDWriteTextFormat* g_fmtSwitchVal = nullptr;
static IDWriteTextFormat* g_fmtUrl       = nullptr;
static IDWriteTextFormat* g_fmtCta       = nullptr;
static IDWriteTextFormat* g_fmtFooter    = nullptr;
static IDWriteTextFormat* g_fmtRight     = nullptr;

static ShareConfig g_cfg;
static ShareState g_state;
static std::mutex g_mu;
static std::atomic<bool> g_quitting{false};
static HANDLE g_stream_proc = nullptr;
static HANDLE g_job = nullptr;
static bool g_copied_feedback = false;
static uint32_t g_copied_tick = 0;
static bool g_saved_url_feedback = false;
static uint32_t g_saved_url_tick = 0;
static bool g_pending_apply = false;
static DWORD g_cached_discord_pid = 0;
static uint32_t g_discord_check_tick = 0;

// Inline Server/Viewer URL editor & smart clipboard URL detection
static bool g_editing_url = false;
static HWND g_edit_hwnd = nullptr;
static WNDPROC g_edit_orig_proc = nullptr;
static HFONT g_edit_font = nullptr;
static HBRUSH g_edit_bg_brush = nullptr;
static std::string g_clip_candidate_url;
static std::string g_clip_candidate_room;
static std::string g_dismissed_clip_url;

// Real QPC delta-time animation & tactile hover/press state
static LARGE_INTEGER g_qpc_freq = {};
static LARGE_INTEGER g_qpc_prev = {};
static float g_time = 0.0f;
static float g_intro_t = 0.0f;
static bool g_anim_running = false;
static bool g_window_minimized = false;

static int g_hover_id = HIT_NONE;
static int g_pressed_id = HIT_NONE;
static int g_focus_id = HIT_NONE;
static bool g_mouse_inside = false;
static float g_mouse_x = 90.0f;
static float g_mouse_y = 148.0f;
static bool g_dragging_bitrate = false;
static int g_drag_start_bitrate = 16;

static std::array<float, HIT_COUNT> g_hover_anim = {};
static std::array<float, HIT_COUNT> g_press_anim = {};
static float g_bitrate_visual = 16.0f;

static inline float ease_out_expo(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return 1.0f - std::pow(2.0f, -10.0f * t);
}

// ============================================================================
// UI Rectangles (Strict 8px / 4px Architectural Grid on 480 x 704 DIPs)
// ============================================================================
struct UiRects {
    // Top architectural header (y: 0..48)
    D2D1_RECT_F themeBtn    = D2D1::RectF(294.0f, 12.0f, 318.0f, 36.0f);
    D2D1_RECT_F motionBtn   = D2D1::RectF(324.0f, 12.0f, 406.0f, 36.0f);
    D2D1_RECT_F minBtn      = D2D1::RectF(414.0f, 12.0f, 438.0f, 36.0f);
    D2D1_RECT_F closeBtn    = D2D1::RectF(444.0f, 12.0f, 468.0f, 36.0f);

    // Group 1: Direct Resolution 4-way bar (y: 254..290)
    D2D1_RECT_F resBar      = D2D1::RectF(24.0f, 254.0f, 456.0f, 290.0f);
    D2D1_RECT_F resSeg[4]   = {
        D2D1::RectF(24.0f,  254.0f, 132.0f, 290.0f),
        D2D1::RectF(132.0f, 254.0f, 240.0f, 290.0f),
        D2D1::RectF(240.0f, 254.0f, 348.0f, 290.0f),
        D2D1::RectF(348.0f, 254.0f, 456.0f, 290.0f)
    };

    // Group 2: Direct FPS (2-way) + Bitrate Presets (5-way) (y: 320..356)
    D2D1_RECT_F fpsBar      = D2D1::RectF(24.0f, 320.0f, 152.0f, 356.0f);
    D2D1_RECT_F fpsSeg[2]   = {
        D2D1::RectF(24.0f, 320.0f, 88.0f,  356.0f),
        D2D1::RectF(88.0f, 320.0f, 152.0f, 356.0f)
    };
    D2D1_RECT_F bitBar      = D2D1::RectF(164.0f, 320.0f, 456.0f, 356.0f);
    D2D1_RECT_F bitSeg[5]   = {
        D2D1::RectF(164.0f, 320.0f, 222.0f, 356.0f),
        D2D1::RectF(222.0f, 320.0f, 281.0f, 356.0f),
        D2D1::RectF(281.0f, 320.0f, 339.0f, 356.0f),
        D2D1::RectF(339.0f, 320.0f, 398.0f, 356.0f),
        D2D1::RectF(398.0f, 320.0f, 456.0f, 356.0f)
    };

    // Group 3: 20-Segment Pixel Bitrate Fader (y: 368..416)
    D2D1_RECT_F bitBox      = D2D1::RectF(24.0f, 368.0f, 456.0f, 416.0f);
    D2D1_RECT_F bitTrack    = D2D1::RectF(100.0f, 380.0f, 376.0f, 404.0f);

    // Group 4: Audio Routing Switches (y: 446..490)
    D2D1_RECT_F audioTile   = D2D1::RectF(24.0f,  446.0f, 235.0f, 490.0f);
    D2D1_RECT_F discordTile = D2D1::RectF(245.0f, 446.0f, 456.0f, 490.0f);

    // Group 5: Viewer Link & Server (y: 496..560)
    D2D1_RECT_F serverBtn       = D2D1::RectF(170.0f, 496.0f, 456.0f, 518.0f);
    D2D1_RECT_F clipPasteBtn    = D2D1::RectF(354.0f, 496.0f, 428.0f, 518.0f);
    D2D1_RECT_F clipDismissBtn  = D2D1::RectF(432.0f, 496.0f, 456.0f, 518.0f);
    D2D1_RECT_F urlWell         = D2D1::RectF(24.0f,  522.0f, 274.0f, 560.0f);
    D2D1_RECT_F copyBtn         = D2D1::RectF(282.0f, 522.0f, 342.0f, 560.0f);
    D2D1_RECT_F newRoomBtn      = D2D1::RectF(348.0f, 522.0f, 398.0f, 560.0f);
    D2D1_RECT_F openWebBtn      = D2D1::RectF(404.0f, 522.0f, 456.0f, 560.0f);

    // Group 6: Primary Action CTA (y: 576..626)
    D2D1_RECT_F startStop   = D2D1::RectF(24.0f, 576.0f, 456.0f, 626.0f);
} g_rects;

static bool pt_in_rect(float x, float y, const D2D1_RECT_F& r) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static int hit_test_ui(float x, float y) {
    if (pt_in_rect(x, y, g_rects.closeBtn))    return HIT_CLOSE;
    if (pt_in_rect(x, y, g_rects.minBtn))      return HIT_MIN;
    if (pt_in_rect(x, y, g_rects.motionBtn))   return HIT_MOTION;
    if (pt_in_rect(x, y, g_rects.themeBtn))    return HIT_THEME;
    for (int i = 0; i < 4; ++i)
        if (pt_in_rect(x, y, g_rects.resSeg[i])) return HIT_RES_0 + i;
    for (int i = 0; i < 2; ++i)
        if (pt_in_rect(x, y, g_rects.fpsSeg[i])) return HIT_FPS_0 + i;
    for (int i = 0; i < 5; ++i)
        if (pt_in_rect(x, y, g_rects.bitSeg[i])) return HIT_BIT_0 + i;
    if (pt_in_rect(x, y, g_rects.bitBox))      return HIT_BITRATE_FADER;
    if (pt_in_rect(x, y, g_rects.audioTile))   return HIT_AUDIO;
    if (pt_in_rect(x, y, g_rects.discordTile)) return HIT_DISCORD;
    if (!g_editing_url && !g_clip_candidate_url.empty()) {
        if (pt_in_rect(x, y, g_rects.clipPasteBtn))   return HIT_CLIP_PASTE;
        if (pt_in_rect(x, y, g_rects.clipDismissBtn)) return HIT_CLIP_DISMISS;
    } else if (!g_editing_url) {
        if (pt_in_rect(x, y, g_rects.serverBtn))      return HIT_SERVER;
    }
    if (pt_in_rect(x, y, g_rects.urlWell))     return HIT_URL_WELL;
    if (pt_in_rect(x, y, g_rects.copyBtn))     return HIT_COPY;
    if (pt_in_rect(x, y, g_rects.newRoomBtn))  return HIT_NEW_ROOM;
    if (pt_in_rect(x, y, g_rects.openWebBtn))  return HIT_OPEN_WEB;
    if (pt_in_rect(x, y, g_rects.startStop))   return HIT_START_STOP;
    return HIT_NONE;
}

// ============================================================================
// String / Path / DPI Utilities
// ============================================================================
static std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(sz - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], sz);
    return ws;
}

static std::string to_utf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(sz - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &s[0], sz, nullptr, nullptr);
    return s;
}

static std::wstring get_app_dir() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    size_t s = p.find_last_of(L"\\/");
    return (s != std::wstring::npos) ? p.substr(0, s) : L".";
}

static float get_window_dpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn && hwnd) {
        UINT dpi = fn(hwnd);
        if (dpi > 0) return static_cast<float>(dpi);
    }
    return 96.0f;
}

static void enable_per_monitor_dpi() {
    using SetDpiCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto fn = reinterpret_cast<SetDpiCtxFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (fn) {
        fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}

static float px_to_dip(HWND hwnd, int px) {
    return static_cast<float>(px) * 96.0f / get_window_dpi(hwnd);
}

static int dip_to_px(HWND hwnd, float dip) {
    return static_cast<int>(std::round(dip * get_window_dpi(hwnd) / 96.0f));
}

// ============================================================================
// GStreamer WHIP Process Lifecycle
// ============================================================================
static std::wstring g_original_path;

static std::wstring resolve_gst_bin(std::wstring& out_bin_dir) {
    std::wstring app_dir = get_app_dir();
    const std::wstring candidates[] = {
        app_dir + L"\\gst\\bin\\gst-launch-1.0.exe",
        app_dir + L"\\bin\\gst-launch-1.0.exe",
        app_dir + L"\\gst-launch-1.0.exe",
        L"C:\\msys64\\ucrt64\\bin\\gst-launch-1.0.exe",
    };
    for (const auto& path : candidates) {
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            size_t slash = path.find_last_of(L"\\/");
            out_bin_dir = (slash == std::wstring::npos) ? app_dir : path.substr(0, slash);
            return path;
        }
    }
    out_bin_dir = L"";
    return L"gst-launch-1.0.exe";
}

static void prepare_gst_env(const std::wstring& bin_dir) {
    if (g_original_path.empty()) {
        wchar_t old_path[32767] = {};
        DWORD n = GetEnvironmentVariableW(L"PATH", old_path, 32767);
        if (n > 0 && n < 32767) g_original_path = old_path;
    }
    if (bin_dir.empty()) return;

    std::wstring new_path = bin_dir + L";" + g_original_path;
    SetEnvironmentVariableW(L"PATH", new_path.c_str());

    std::wstring prefix = bin_dir;
    size_t slash = prefix.find_last_of(L"\\/");
    if (slash != std::wstring::npos) prefix = prefix.substr(0, slash);
    std::wstring plugins = prefix + L"\\lib\\gstreamer-1.0";
    if (GetFileAttributesW(plugins.c_str()) != INVALID_FILE_ATTRIBUTES) {
        SetEnvironmentVariableW(L"GST_PLUGIN_SYSTEM_PATH_1_0", plugins.c_str());
        SetEnvironmentVariableW(L"GST_PLUGIN_PATH_1_0", plugins.c_str());
    }
}

static std::string read_log_tail(const std::wstring& path) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return "";
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return ""; }
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return ""; }
    long start = (sz > 500) ? (sz - 500) : 0;
    fseek(f, start, SEEK_SET);
    std::string data((size_t)(sz - start), '\0');
    size_t n = fread(data.data(), 1, data.size(), f);
    fclose(f);
    data.resize(n);
    while (!data.empty() && (data.back() == '\n' || data.back() == '\r' || data.back() == ' ')) data.pop_back();
    size_t pos = data.find_last_of("\r\n");
    if (pos != std::string::npos) data = data.substr(pos + 1);
    if (data.size() > 140) data = data.substr(data.size() - 140);
    return data;
}

static void load_config() {
    std::wstring cfg_path = get_app_dir() + L"\\server.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, cfg_path.c_str(), L"r") == 0 && f) {
        char buf[512] = {};
        if (fgets(buf, sizeof(buf), f)) {
            std::string s = buf;
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            if (!s.empty()) g_cfg.server_url = s;
        }
        fclose(f);
    } else if (_wfopen_s(&f, cfg_path.c_str(), L"w") == 0 && f) {
        fputs("http://localhost:8889\n", f);
        fclose(f);
    }
}

static void save_config() {
    std::wstring cfg_path = get_app_dir() + L"\\server.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, cfg_path.c_str(), L"w") == 0 && f) {
        fputs(g_cfg.server_url.c_str(), f);
        fputc('\n', f);
        fclose(f);
    }
}

static void stop_stream_process() {
    HANDLE p = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        p = g_stream_proc;
        g_stream_proc = nullptr;
        g_state.is_streaming = false;
        g_state.status_text = "Standby";
    }
    if (p) {
        TerminateProcess(p, 0);
        WaitForSingleObject(p, 1000);
        CloseHandle(p);
    }
    if (g_hwnd) PostMessageW(g_hwnd, WM_STREAM_STATE, 0, 0);
}

static void start_stream_process() {
    stop_stream_process();
    if (g_quitting.load()) return;

    ShareConfig cfg_copy;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        cfg_copy = g_cfg;
        g_state.is_streaming = true;
        g_state.start_time = GetTickCount();
        g_state.status_text = "Connecting";
        g_state.error_msg.clear();
    }
    if (g_hwnd) PostMessageW(g_hwnd, WM_STREAM_STATE, 0, 0);

    std::thread([cfg_copy]() {
        std::wstring bin_dir;
        std::wstring gst_bin = resolve_gst_bin(bin_dir);
        std::string pipe_args = build_whip_pipeline(cfg_copy);
        std::wstring cmd = L"\"" + gst_bin + L"\" -e " + to_wide(pipe_args);

        std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back(L'\0');

        prepare_gst_env(bin_dir);
        if (!bin_dir.empty() && GetFileAttributesW(gst_bin.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::lock_guard<std::mutex> lk(g_mu);
            g_state.is_streaming = false;
            g_state.status_text = "Error";
            g_state.error_msg = "Streaming runtime missing from app folder.";
            if (g_hwnd) PostMessageW(g_hwnd, WM_STREAM_STATE, 0, 0);
            return;
        }

        std::wstring log_path = get_app_dir() + L"\\stream_log.txt";
        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        HANDLE hLog = CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (hLog != INVALID_HANDLE_VALUE) {
            si.dwFlags |= STARTF_USESTDHANDLES;
            si.hStdOutput = hLog;
            si.hStdError = hLog;
        }

        PROCESS_INFORMATION pi = {};
        LPCWSTR work_dir = bin_dir.empty() ? nullptr : bin_dir.c_str();
        BOOL ok = CreateProcessW(
            nullptr, cmd_buf.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, work_dir, &si, &pi
        );
        if (hLog != INVALID_HANDLE_VALUE) CloseHandle(hLog);

        if (!ok) {
            std::lock_guard<std::mutex> lk(g_mu);
            g_state.is_streaming = false;
            g_state.status_text = "Error";
            g_state.error_msg = "Could not start bundled capture process.";
            if (g_hwnd) PostMessageW(g_hwnd, WM_STREAM_STATE, 0, 0);
            return;
        }

        if (g_job) AssignProcessToJobObject(g_job, pi.hProcess);
        CloseHandle(pi.hThread);

        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_stream_proc = pi.hProcess;
            g_state.status_text = "Live";
        }
        if (g_hwnd) PostMessageW(g_hwnd, WM_STREAM_STATE, 0, 0);

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);

        std::string tail = read_log_tail(log_path);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_stream_proc = nullptr;
            bool unexpected = (g_state.status_text == "Live" || g_state.status_text == "Connecting");
            g_state.is_streaming = false;
            if (unexpected) {
                g_state.status_text = "Ended";
                if (!tail.empty()) g_state.error_msg = tail;
            }
        }
        if (g_hwnd) PostMessageW(g_hwnd, WM_STREAM_STATE, 0, 0);
    }).detach();
}

// ============================================================================
// Direct2D / DirectWrite Initialization & Cached Brush
// ============================================================================
static void discard_d2d_targets() {
    if (g_br) { g_br->Release(); g_br = nullptr; }
    if (g_rt) { g_rt->Release(); g_rt = nullptr; }
}

static void release_all_d2d() {
    discard_d2d_targets();
    for (auto** fmt : {
        &g_fmtBrand, &g_fmtTag, &g_fmtTitle, &g_fmtSub, &g_fmtSeg,
        &g_fmtMicro, &g_fmtSwitchVal, &g_fmtUrl, &g_fmtCta, &g_fmtFooter, &g_fmtRight
    }) {
        if (*fmt) { (*fmt)->Release(); *fmt = nullptr; }
    }
    if (g_dw)  { g_dw->Release();  g_dw = nullptr; }
    if (g_d2d) { g_d2d->Release(); g_d2d = nullptr; }
}

static HRESULT init_d2d(HWND hwnd) {
    if (!g_d2d) {
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d);
    }
    if (!g_dw) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_dw));
        g_fonts = StudioTheme::load_fonts(g_dw);

        auto make_fmt = [&](const std::wstring& fam, DWRITE_FONT_WEIGHT wt, float sz,
                            DWRITE_TEXT_ALIGNMENT halign, IDWriteTextFormat** out) {
            g_dw->CreateTextFormat(
                fam.c_str(), nullptr, wt, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                sz, L"en-us", out);
            if (*out) {
                (*out)->SetTextAlignment(halign);
                (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                (*out)->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            }
        };

        make_fmt(g_fonts.monoFamily, DWRITE_FONT_WEIGHT_BOLD,      12.5f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtBrand);
        make_fmt(g_fonts.monoFamily, DWRITE_FONT_WEIGHT_SEMI_BOLD,  9.5f, DWRITE_TEXT_ALIGNMENT_CENTER,   &g_fmtTag);
        make_fmt(g_fonts.bodyFamily, DWRITE_FONT_WEIGHT_SEMI_BOLD, 20.5f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtTitle);
        make_fmt(g_fonts.monoFamily, DWRITE_FONT_WEIGHT_REGULAR,   10.5f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtSub);
        make_fmt(g_fonts.monoFamily, DWRITE_FONT_WEIGHT_BOLD,      11.2f, DWRITE_TEXT_ALIGNMENT_CENTER,   &g_fmtSeg);
        make_fmt(g_fonts.monoFamily, DWRITE_FONT_WEIGHT_SEMI_BOLD,  9.2f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtMicro);
        make_fmt(g_fonts.monoFamily, DWRITE_FONT_WEIGHT_BOLD,      11.5f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtSwitchVal);
        make_fmt(g_fonts.monoFamily, DWRITE_FONT_WEIGHT_MEDIUM,    10.8f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtUrl);
        make_fmt(g_fonts.monoFamily, DWRITE_FONT_WEIGHT_BOLD,      13.0f, DWRITE_TEXT_ALIGNMENT_CENTER,   &g_fmtCta);
        make_fmt(g_fonts.monoFamily, DWRITE_FONT_WEIGHT_MEDIUM,    10.0f, DWRITE_TEXT_ALIGNMENT_LEADING,  &g_fmtFooter);
        make_fmt(g_fonts.monoFamily, DWRITE_FONT_WEIGHT_SEMI_BOLD, 10.0f, DWRITE_TEXT_ALIGNMENT_TRAILING, &g_fmtRight);
    }

    if (!g_rt) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        UINT32 w = std::max<UINT32>(1, (UINT32)(rc.right - rc.left));
        UINT32 h = std::max<UINT32>(1, (UINT32)(rc.bottom - rc.top));
        float dpi = get_window_dpi(hwnd);
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        props.dpiX = dpi;
        props.dpiY = dpi;
        HRESULT hr = g_d2d->CreateHwndRenderTarget(
            props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(w, h)), &g_rt);
        if (SUCCEEDED(hr) && g_rt) {
            g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
            g_rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::PALETTE_LIGHT.ink), &g_br);
        }
    }
    return (g_rt && g_br) ? S_OK : E_FAIL;
}

// ============================================================================
// Crisp Pixel-Art & Architectural Drawing Primitives
// ============================================================================
static inline void fill_px(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                           float x, float y, float w, float h, uint32_t hex, float alpha = 1.0f) {
    br->SetColor(StudioTheme::color(hex, alpha));
    rt->FillRectangle(D2D1::RectF(std::floor(x), std::floor(y), std::floor(x + w), std::floor(y + h)), br);
}

static inline void draw_rect_1px(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                                 const D2D1_RECT_F& r, uint32_t hex, float alpha = 1.0f, float stroke = 1.0f) {
    br->SetColor(StudioTheme::color(hex, alpha));
    D2D1_RECT_F snapped = D2D1::RectF(
        std::floor(r.left) + 0.5f, std::floor(r.top) + 0.5f,
        std::floor(r.right) - 0.5f, std::floor(r.bottom) - 0.5f);
    rt->DrawRectangle(snapped, br, stroke);
}

static inline D2D1_RECT_F offset_rect(const D2D1_RECT_F& r, float dx, float dy) {
    return D2D1::RectF(r.left + dx, r.top + dy, r.right + dx, r.bottom + dy);
}

static void draw_theme_icon(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                            const D2D1_RECT_F& r, bool darkMode, uint32_t col) {
    static const char* moon[7] = {
        "..XXX..",
        ".XX....",
        "XX.....",
        "XX.....",
        "XX....X",
        ".XX..XX",
        "..XXXX."
    };
    static const char* sun[7] = {
        "...X...",
        ".X...X.",
        "..XXX..",
        "X.XXX.X",
        "..XXX..",
        ".X...X.",
        "...X..."
    };
    const char** glyph = darkMode ? sun : moon;
    float u = 2.0f;
    float x0 = std::floor((r.left + r.right) * 0.5f - 3.5f * u);
    float y0 = std::floor((r.top + r.bottom) * 0.5f - 3.5f * u);
    for (int row = 0; row < 7; ++row) {
        for (int c = 0; c < 7; ++c) {
            if (glyph[row][c] == 'X') fill_px(rt, br, x0 + c * u, y0 + row * u, u, u, col, 1.0f);
        }
    }
}

static void draw_mini_pixel_ghost(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                                  const StudioTheme::Palette& pal, float x0, float y0, float u, uint32_t statusCol) {
    static const char* rows[8] = {
        "..XXXX..",
        ".XXXXXX.",
        "XXXXXXXX",
        "X.XXXX.X",
        "XXXXXXXX",
        "XXXXXXXX",
        "XXXXXXXX",
        "X.X..X.X"
    };
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            if (rows[r][c] == 'X') fill_px(rt, br, x0 + c * u, y0 + r * u, u, u, pal.ink, 1.0f);
        }
    }
    fill_px(rt, br, x0 + 6 * u, y0 + 6 * u, u * 1.5f, u * 1.5f, statusCol, 1.0f);
}

static void draw_hero_pixel_ghost(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                                  const StudioTheme::Palette& pal, float cx, float cy, float u,
                                  bool isLive, bool isError, int frame, bool blink, int eye_dx, int eye_dy, float alpha) {
    static const char* body_f0[14] = {
        ".....######.....",
        "...##WWWWWW##...",
        "..#WWWWWWWWWW#..",
        ".#WWWWWWWWWWWW#.",
        ".#WWWWWWWWWWWW#.",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWW#WWWWWW#WWW#",
        "#WW#.#WWWW#.#WW#",
        ".##...####...##."
    };
    static const char* body_f1[14] = {
        ".....######.....",
        "...##WWWWWW##...",
        "..#WWWWWWWWWW#..",
        ".#WWWWWWWWWWWW#.",
        ".#WWWWWWWWWWWW#.",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WWWWWWWWWWWWWW#",
        "#WW#WWWWWWWW#WW#",
        ".#W#.#WWWW#.#W#.",
        "..##..####..##.."
    };

    const char** sprite = (frame % 2 == 0) ? body_f0 : body_f1;
    float ox = std::floor(cx - 8.0f * u);
    float oy = std::floor(cy - 8.0f * u);

    for (int r = 0; r < 14; ++r) {
        for (int c = 0; c < 16; ++c) {
            if (sprite[r][c] != '.') {
                fill_px(rt, br, ox + c * u + 3.0f, oy + r * u + 3.0f, u, u, pal.ghost_shadow, 0.65f * alpha);
            }
        }
    }
    for (int r = 0; r < 14; ++r) {
        for (int c = 0; c < 16; ++c) {
            char ch = sprite[r][c];
            if (ch == '#') fill_px(rt, br, ox + c * u, oy + r * u, u, u, pal.ghost_outline, alpha);
            else if (ch == 'W') fill_px(rt, br, ox + c * u, oy + r * u, u, u, pal.ghost_fill, alpha);
        }
    }

    if (isError) {
        uint32_t ec = pal.accent_vermilion;
        fill_px(rt, br, ox + 4 * u, oy + 5 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 5 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 5 * u, oy + 6 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 4 * u, oy + 7 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 7 * u, u, u, ec, alpha);

        fill_px(rt, br, ox + 9 * u, oy + 5 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 11 * u, oy + 5 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 10 * u, oy + 6 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 9 * u, oy + 7 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 11 * u, oy + 7 * u, u, u, ec, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 9 * u, 4 * u, u, pal.ghost_eyes, alpha);
    } else if (isLive) {
        fill_px(rt, br, ox + 5 * u, oy + 5 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 4 * u, oy + 6 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 6 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 10 * u, oy + 5 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 9 * u, oy + 6 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 11 * u, oy + 6 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 3 * u, oy + 8 * u, u, u, pal.pixel_mint, alpha);
        fill_px(rt, br, ox + 12 * u, oy + 8 * u, u, u, pal.pixel_mint, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 8 * u, u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 7 * u, oy + 9 * u, 2 * u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 9 * u, oy + 8 * u, u, u, pal.ghost_eyes, alpha);
    } else if (blink) {
        fill_px(rt, br, ox + 4 * u, oy + 6 * u, 3 * u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 9 * u, oy + 6 * u, 3 * u, u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 7 * u, oy + 9 * u, 2 * u, u, pal.ghost_eyes, alpha);
    } else {
        float lx = ox + (5 + eye_dx) * u;
        float rx = ox + (10 + eye_dx) * u;
        float ey = oy + (6 + eye_dy) * u;
        fill_px(rt, br, lx, ey, 2 * u, 2 * u, pal.ghost_eyes, alpha);
        fill_px(rt, br, rx, ey, 2 * u, 2 * u, pal.ghost_eyes, alpha);
        fill_px(rt, br, ox + 6 * u, oy + 9 * u, 4 * u, u, pal.ghost_eyes, alpha);
    }

    uint32_t prismCol = isLive ? pal.pixel_mint : pal.accent_vermilion;
    float py = oy + 15.0f * u;
    fill_px(rt, br, ox + 7 * u, py, 2 * u, u, prismCol, alpha);
    fill_px(rt, br, ox + 6 * u, py + u, 4 * u, u, prismCol, alpha);
    fill_px(rt, br, ox + 5 * u, py + 2 * u, 6 * u, u, prismCol, alpha);
}

static void draw_segmented_bar(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, const StudioTheme::Palette& pal,
                               const D2D1_RECT_F& outerRect, const D2D1_RECT_F* segRects,
                               int firstHitId, int count, int selectedIdx,
                               const wchar_t* const* labels, float yOffset, float alpha) {
    D2D1_RECT_F frame = offset_rect(outerRect, 0.0f, yOffset);
    br->SetColor(StudioTheme::color(pal.surface, alpha));
    rt->FillRectangle(frame, br);

    for (int i = 0; i < count; ++i) {
        int hid = firstHitId + i;
        float hv = g_hover_anim[hid];
        bool sel = (i == selectedIdx);
        D2D1_RECT_F sr = offset_rect(segRects[i], 0.0f, yOffset);

        if (sel) {
            br->SetColor(StudioTheme::color(pal.ink, alpha));
            rt->FillRectangle(sr, br);
            fill_px(rt, br, sr.left, sr.bottom - 2.0f, sr.right - sr.left, 2.0f, pal.accent_vermilion, alpha);
        } else if (hv > 0.02f) {
            br->SetColor(StudioTheme::color(pal.surface_muted, alpha * hv));
            rt->FillRectangle(sr, br);
        }

        if (i > 0) {
            br->SetColor(StudioTheme::color(pal.border, alpha));
            rt->DrawLine(D2D1::Point2F(std::floor(sr.left) + 0.5f, sr.top + 1.0f),
                         D2D1::Point2F(std::floor(sr.left) + 0.5f, sr.bottom - 1.0f), br, 1.0f);
        }

        br->SetColor(StudioTheme::color(sel ? pal.ink_on_active : (hv > 0.25f ? pal.ink : pal.ink_muted), alpha));
        rt->DrawTextW(labels[i], (UINT32)wcslen(labels[i]), g_fmtSeg, sr, br);

        if (g_focus_id == hid) {
            draw_rect_1px(rt, br, D2D1::RectF(sr.left + 2.0f, sr.top + 2.0f, sr.right - 2.0f, sr.bottom - 2.0f), pal.accent_vermilion, alpha);
        }
    }

    draw_rect_1px(rt, br, frame, pal.border_strong, alpha);
}

static void draw_switch_tile(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, const StudioTheme::Palette& pal,
                             int hitId, const D2D1_RECT_F& baseRect, float yOffset, float alpha,
                             const wchar_t* microLabel, const wchar_t* valueText,
                             bool active, bool disabled, uint32_t ledColor) {
    float hv = disabled ? 0.0f : g_hover_anim[hitId];
    float pr = disabled ? 0.0f : g_press_anim[hitId];
    float lift = -2.0f * hv + 2.0f * pr;

    D2D1_RECT_F r = offset_rect(baseRect, lift, yOffset + lift);

    if (hv > 0.02f) {
        D2D1_RECT_F sh = offset_rect(baseRect, 3.0f * hv, yOffset + 3.0f * hv);
        br->SetColor(StudioTheme::color(pal.shadow_ink, alpha * hv));
        rt->FillRectangle(sh, br);
    }

    br->SetColor(StudioTheme::color((active && !disabled) ? pal.surface : pal.surface_muted, alpha));
    rt->FillRectangle(r, br);
    draw_rect_1px(rt, br, r, (!disabled && (hv > 0.25f || active)) ? pal.border_strong : pal.border, alpha);

    br->SetColor(StudioTheme::color(pal.ink_subtle, alpha));
    rt->DrawTextW(microLabel, (UINT32)wcslen(microLabel), g_fmtMicro,
                  D2D1::RectF(r.left + 10.0f, r.top + 5.0f, r.right - 22.0f, r.top + 19.0f), br);

    fill_px(rt, br, r.right - 14.0f, r.top + 9.0f, 5.0f, 5.0f, (active && !disabled) ? ledColor : pal.border, alpha);

    br->SetColor(StudioTheme::color((active && !disabled) ? pal.ink : pal.ink_muted, alpha));
    rt->DrawTextW(valueText, (UINT32)wcslen(valueText), g_fmtSwitchVal,
                  D2D1::RectF(r.left + 10.0f, r.top + 20.0f, r.right - 8.0f, r.bottom - 4.0f), br);

    if (g_focus_id == hitId) {
        draw_rect_1px(rt, br, D2D1::RectF(r.left - 2.0f, r.top - 2.0f, r.right + 2.0f, r.bottom + 2.0f), pal.accent_vermilion, alpha);
    }
}

static void draw_action_chip(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br, const StudioTheme::Palette& pal,
                             int hitId, const D2D1_RECT_F& baseRect, float yOffset, float alpha,
                             const wchar_t* label, bool highlighted) {
    float hv = g_hover_anim[hitId];
    float pr = g_press_anim[hitId];
    float lift = -2.0f * hv + 2.0f * pr;

    D2D1_RECT_F r = offset_rect(baseRect, lift, yOffset + lift);

    if (hv > 0.02f) {
        D2D1_RECT_F sh = offset_rect(baseRect, 3.0f * hv, yOffset + 3.0f * hv);
        br->SetColor(StudioTheme::color(highlighted ? pal.pixel_mint : pal.shadow_ink, alpha * hv));
        rt->FillRectangle(sh, br);
    }

    uint32_t bg = highlighted ? pal.pixel_mint : (hv > 0.45f ? pal.ink : pal.surface);
    uint32_t fg = highlighted ? 0x0E0F12 : (hv > 0.45f ? pal.ink_on_active : pal.ink);
    uint32_t bdr = highlighted ? pal.pixel_mint : pal.border_strong;

    br->SetColor(StudioTheme::color(bg, alpha));
    rt->FillRectangle(r, br);
    draw_rect_1px(rt, br, r, bdr, alpha);

    br->SetColor(StudioTheme::color(fg, alpha));
    rt->DrawTextW(label, (UINT32)wcslen(label), g_fmtTag, r, br);

    if (g_focus_id == hitId) {
        draw_rect_1px(rt, br, D2D1::RectF(r.left - 2.0f, r.top - 2.0f, r.right + 2.0f, r.bottom + 2.0f), pal.accent_vermilion, alpha);
    }
}

// ============================================================================
// Main Direct2D Scene Renderer
// ============================================================================
static void draw_scene(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* br,
                       const ShareConfig& cfg, const ShareState& st) {
    const auto& pal = StudioTheme::palette(cfg.dark_mode);

    auto stagger = [&](float delay) -> float {
        if (!cfg.motion_enabled) return 1.0f;
        return ease_out_expo(std::clamp((g_intro_t - delay) / 0.34f, 0.0f, 1.0f));
    };

    float pHero = stagger(0.00f);
    float pRes  = stagger(0.05f);
    float pFps  = stagger(0.09f);
    float pBit  = stagger(0.13f);
    float pAud  = stagger(0.17f);
    float pLink = stagger(0.21f);
    float pCta  = stagger(0.25f);

    bool isLive = (st.is_streaming && st.status_text == "Live");
    bool isConn = (st.is_streaming && st.status_text == "Connecting");
    bool isErr  = (!st.error_msg.empty());

    uint32_t statusAccent = pal.pixel_cyan;
    std::wstring statusTag = L"STANDBY";
    if (g_pending_apply)   { statusAccent = pal.accent_vermilion; statusTag = L"SYNCING"; }
    else if (isLive)       { statusAccent = pal.pixel_mint;       statusTag = L"LIVE"; }
    else if (isConn)       { statusAccent = pal.accent_vermilion; statusTag = L"CONNECT"; }
    else if (isErr)        { statusAccent = pal.accent_vermilion; statusTag = L"ERROR"; }

    if (st.is_streaming) {
        uint32_t sec = (GetTickCount() - st.start_time) / 1000;
        wchar_t tbuf[24];
        swprintf_s(tbuf, L" \u00B7 %02u:%02u", sec / 60, sec % 60);
        statusTag += tbuf;
    }

    rt->BeginDraw();
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    rt->Clear(StudioTheme::color(pal.paper_bg));

    // 1. Drafting Graph-Paper Grid (Hero Canvas y: 48..224)
    const float pitch = 16.0f;
    br->SetColor(StudioTheme::color(pal.grid_line, 0.90f));
    for (float gx = pitch; gx < LOGICAL_W; gx += pitch) {
        rt->DrawLine(D2D1::Point2F(gx + 0.5f, 48.0f), D2D1::Point2F(gx + 0.5f, 224.0f), br, 1.0f);
    }
    for (float gy = 48.0f + pitch; gy < 224.0f; gy += pitch) {
        rt->DrawLine(D2D1::Point2F(0.0f, gy + 0.5f), D2D1::Point2F(LOGICAL_W, gy + 0.5f), br, 1.0f);
    }
    br->SetColor(StudioTheme::color(pal.grid_major, 0.95f));
    for (float gx = 64.0f; gx < LOGICAL_W; gx += 64.0f) {
        for (float gy = 96.0f; gy < 210.0f; gy += 64.0f) {
            rt->DrawLine(D2D1::Point2F(gx - 3.0f, gy + 0.5f), D2D1::Point2F(gx + 4.0f, gy + 0.5f), br, 1.0f);
            rt->DrawLine(D2D1::Point2F(gx + 0.5f, gy - 3.0f), D2D1::Point2F(gx + 0.5f, gy + 4.0f), br, 1.0f);
        }
    }

    // 2. Top Architectural Bar (y: 0..48)
    br->SetColor(StudioTheme::color(pal.surface));
    rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, LOGICAL_W, 48.0f), br);
    br->SetColor(StudioTheme::color(pal.border));
    rt->DrawLine(D2D1::Point2F(0.0f, 48.5f), D2D1::Point2F(LOGICAL_W, 48.5f), br, 1.0f);

    draw_mini_pixel_ghost(rt, br, pal, 16.0f, 16.0f, 2.0f, statusAccent);

    br->SetColor(StudioTheme::color(pal.ink));
    const wchar_t* brandTxt = L"GHOST SHARE JOR";
    rt->DrawTextW(brandTxt, (UINT32)wcslen(brandTxt), g_fmtBrand,
                  D2D1::RectF(40.0f, 0.0f, 240.0f, 48.0f), br);

    // Compact Dark/Light Theme Button right beside MOTION
    {
        float hv = g_hover_anim[HIT_THEME];
        D2D1_RECT_F tr = g_rects.themeBtn;
        br->SetColor(StudioTheme::color(hv > 0.25f ? pal.ink : (cfg.dark_mode ? pal.paper_bg : pal.surface_muted)));
        rt->FillRectangle(tr, br);
        draw_rect_1px(rt, br, tr, (hv > 0.25f || cfg.dark_mode) ? pal.border_strong : pal.border);
        draw_theme_icon(rt, br, tr, cfg.dark_mode, hv > 0.25f ? pal.ink_on_active : pal.ink);
    }

    // Header MOTION toggle
    {
        float hv = g_hover_anim[HIT_MOTION];
        D2D1_RECT_F mr = g_rects.motionBtn;
        bool on = cfg.motion_enabled;
        br->SetColor(StudioTheme::color(on ? pal.ink : (hv > 0.2f ? pal.surface : pal.surface_muted)));
        rt->FillRectangle(mr, br);
        draw_rect_1px(rt, br, mr, (on || hv > 0.2f) ? pal.border_strong : pal.border);
        fill_px(rt, br, mr.left + 8.0f, mr.top + 9.0f, 5.0f, 5.0f, on ? pal.pixel_mint : pal.ink_subtle);
        br->SetColor(StudioTheme::color(on ? pal.ink_on_active : pal.ink_muted));
        const wchar_t* mTxt = on ? L"MOTION" : L"STATIC";
        rt->DrawTextW(mTxt, (UINT32)wcslen(mTxt), g_fmtTag,
                      D2D1::RectF(mr.left + 15.0f, mr.top, mr.right - 4.0f, mr.bottom), br);
    }

    // Minimize & Close Buttons
    {
        float hv = g_hover_anim[HIT_MIN];
        if (hv > 0.05f) {
            br->SetColor(StudioTheme::color(pal.surface_muted, hv));
            rt->FillRectangle(g_rects.minBtn, br);
        }
        draw_rect_1px(rt, br, g_rects.minBtn, hv > 0.3f ? pal.border_strong : pal.border);
        fill_px(rt, br, g_rects.minBtn.left + 7.0f, g_rects.minBtn.top + 15.0f, 10.0f, 2.0f, pal.ink);
    }
    {
        float hv = g_hover_anim[HIT_CLOSE];
        if (hv > 0.05f) {
            br->SetColor(StudioTheme::color(pal.accent_vermilion, hv));
            rt->FillRectangle(g_rects.closeBtn, br);
        }
        draw_rect_1px(rt, br, g_rects.closeBtn, hv > 0.3f ? pal.accent_vermilion : pal.border);
        uint32_t xCol = (hv > 0.45f) ? 0xFFFFFF : pal.ink;
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        br->SetColor(StudioTheme::color(xCol));
        rt->DrawLine(D2D1::Point2F(g_rects.closeBtn.left + 7.0f, g_rects.closeBtn.top + 7.0f),
                     D2D1::Point2F(g_rects.closeBtn.right - 7.0f, g_rects.closeBtn.bottom - 7.0f), br, 1.6f);
        rt->DrawLine(D2D1::Point2F(g_rects.closeBtn.right - 7.0f, g_rects.closeBtn.top + 7.0f),
                     D2D1::Point2F(g_rects.closeBtn.left + 7.0f, g_rects.closeBtn.bottom - 7.0f), br, 1.6f);
        rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    }

    // 3. Asymmetric Architectural Hero Stage (y: 48..224)
    float heroDy = (1.0f - pHero) * 8.0f;

    // Top-left live status badge
    {
        float badgeW = st.is_streaming ? 140.0f : 104.0f;
        D2D1_RECT_F badge = D2D1::RectF(24.0f, 60.0f + heroDy, 24.0f + badgeW, 82.0f + heroDy);
        br->SetColor(StudioTheme::color(pal.surface, pHero));
        rt->FillRectangle(badge, br);
        draw_rect_1px(rt, br, badge, pal.border_strong, pHero);
        bool pulseOn = !cfg.motion_enabled || (std::fmod(g_time, 1.1f) < 0.72f) || isLive;
        fill_px(rt, br, badge.left + 8.0f, badge.top + 8.0f, 5.0f, 5.0f, statusAccent, pulseOn ? pHero : 0.25f * pHero);
        br->SetColor(StudioTheme::color(pal.ink, pHero));
        rt->DrawTextW(statusTag.c_str(), (UINT32)statusTag.size(), g_fmtTag,
                      D2D1::RectF(badge.left + 15.0f, badge.top, badge.right - 4.0f, badge.bottom), br);
    }

    // Top-right live profile telemetry
    {
        const wchar_t* shortRes = (cfg.resolution == ScreenResolution::Source) ? L"NATIVE"
                                : (cfg.resolution == ScreenResolution::Res1440p) ? L"1440p"
                                : (cfg.resolution == ScreenResolution::Res1080p) ? L"1080p" : L"720p";
        std::wstring spec = std::wstring(shortRes) + L" \u00B7 " +
                            std::to_wstring((int)cfg.fps) + L"FPS \u00B7 " +
                            std::to_wstring(cfg.bitrate_mbps) + L"M";
        br->SetColor(StudioTheme::color(pal.ink_muted, pHero));
        rt->DrawTextW(spec.c_str(), (UINT32)spec.size(), g_fmtRight,
                      D2D1::RectF(210.0f, 60.0f + heroDy, 456.0f, 82.0f + heroDy), br);
    }

    // Left Framed Pixel Ghost Mascot (x: 24..140, y: 98..206)
    {
        D2D1_RECT_F hr = D2D1::RectF(24.0f, 96.0f + heroDy, 140.0f, 206.0f + heroDy);
        float arm = 10.0f;
        uint32_t bCol = isLive ? pal.pixel_mint : pal.grid_major;
        fill_px(rt, br, hr.left, hr.top, arm, 2.0f, bCol, pHero);
        fill_px(rt, br, hr.left, hr.top, 2.0f, arm, bCol, pHero);
        fill_px(rt, br, hr.right - arm, hr.top, arm, 2.0f, bCol, pHero);
        fill_px(rt, br, hr.right - 2.0f, hr.top, 2.0f, arm, bCol, pHero);
        fill_px(rt, br, hr.left, hr.bottom - 2.0f, arm, 2.0f, bCol, pHero);
        fill_px(rt, br, hr.left, hr.bottom - arm, 2.0f, arm, bCol, pHero);
        fill_px(rt, br, hr.right - arm, hr.bottom - 2.0f, arm, 2.0f, bCol, pHero);
        fill_px(rt, br, hr.right - 2.0f, hr.bottom - arm, 2.0f, arm, bCol, pHero);

        float bob = cfg.motion_enabled ? std::round(std::sin(g_time * 3.0f) * 2.5f) : 0.0f;
        int frame = cfg.motion_enabled ? ((int)(g_time * 2.2f) % 2) : 0;
        bool blink = cfg.motion_enabled && (std::fmod(g_time, 3.8f) > 3.66f);
        int eye_dx = 0, eye_dy = 0;
        if (g_mouse_inside) {
            float dx = g_mouse_x - 82.0f;
            float dy = g_mouse_y - 146.0f;
            if (dx > 28.0f) eye_dx = 1; else if (dx < -28.0f) eye_dx = -1;
            if (dy > 28.0f) eye_dy = 1; else if (dy < -28.0f) eye_dy = -1;
        }
        draw_hero_pixel_ghost(rt, br, pal, 82.0f, 144.0f + bob + heroDy, 4.0f,
                              isLive, isErr, frame, blink, eye_dx, eye_dy, pHero);
    }

    // Right Asymmetric Status Copy + Live Equalizer + Room Tag
    {
        std::wstring headline = L"Ready to Share";
        std::wstring subline  = L"WebRTC WHIP \u00B7 D3D11 + WASAPI";
        uint32_t subCol = pal.ink_muted;

        if (isLive) {
            headline = L"Streaming Live";
            subline  = cfg.audio_enabled ? L"Broadcasting screen + audio" : L"Broadcasting video only";
        } else if (isConn) {
            headline = L"Connecting\u2026";
            subline  = L"Starting GStreamer WHIP pipeline";
        } else if (isErr) {
            headline = L"Stream Stopped";
            subline  = to_wide(st.error_msg);
            subCol   = pal.accent_vermilion;
        }

        br->SetColor(StudioTheme::color(pal.ink, pHero));
        rt->DrawTextW(headline.c_str(), (UINT32)headline.size(), g_fmtTitle,
                      D2D1::RectF(158.0f, 102.0f + heroDy, 456.0f, 134.0f + heroDy), br);

        br->SetColor(StudioTheme::color(subCol, pHero));
        rt->DrawTextW(subline.c_str(), (UINT32)subline.size(), g_fmtSub,
                      D2D1::RectF(158.0f, 136.0f + heroDy, 456.0f, 160.0f + heroDy), br);

        // Room tag pill
        std::wstring roomTag = L"ROOM // " + to_wide(cfg.room_id);
        D2D1_RECT_F rBox = D2D1::RectF(158.0f, 170.0f + heroDy, 330.0f, 194.0f + heroDy);
        br->SetColor(StudioTheme::color(pal.surface, pHero));
        rt->FillRectangle(rBox, br);
        draw_rect_1px(rt, br, rBox, pal.border, pHero);
        br->SetColor(StudioTheme::color(pal.ink_muted, pHero));
        rt->DrawTextW(roomTag.c_str(), (UINT32)roomTag.size(), g_fmtTag, rBox, br);

        // Live 5-bar Mint signal meter when streaming
        if (isLive) {
            for (int b = 0; b < 5; ++b) {
                float wave = cfg.motion_enabled
                    ? (0.35f + 0.65f * std::abs(std::sin(g_time * 6.5f + b * 1.15f)))
                    : 0.6f;
                float bh = std::round(4.0f + wave * 16.0f);
                fill_px(rt, br, 344.0f + b * 6.0f, 192.0f - bh + heroDy, 4.0f, bh, pal.pixel_mint, pHero);
            }
        }
    }

    br->SetColor(StudioTheme::color(pal.border));
    rt->DrawLine(D2D1::Point2F(0.0f, 224.5f), D2D1::Point2F(LOGICAL_W, 224.5f), br, 1.0f);

    // 4. Group 1: Direct Resolution Segmented Bar
    {
        float dy = (1.0f - pRes) * 8.0f;
        br->SetColor(StudioTheme::color(pal.ink_subtle, pRes));
        rt->DrawTextW(L"RESOLUTION", 10, g_fmtMicro, D2D1::RectF(24.0f, 236.0f + dy, 200.0f, 251.0f + dy), br);

        const wchar_t* dimTxt = (cfg.resolution == ScreenResolution::Source) ? L"NATIVE DISPLAY"
                              : (cfg.resolution == ScreenResolution::Res1440p) ? L"2560x1440"
                              : (cfg.resolution == ScreenResolution::Res1080p) ? L"1920x1080" : L"1280x720";
        br->SetColor(StudioTheme::color(pal.ink_muted, pRes));
        rt->DrawTextW(dimTxt, (UINT32)wcslen(dimTxt), g_fmtRight, D2D1::RectF(260.0f, 236.0f + dy, 456.0f, 251.0f + dy), br);

        static const wchar_t* resLabels[4] = { L"SOURCE", L"1440p", L"1080p", L"720p" };
        draw_segmented_bar(rt, br, pal, g_rects.resBar, g_rects.resSeg, HIT_RES_0, 4,
                           static_cast<int>(cfg.resolution), resLabels, dy, pRes);
    }

    // 5. Group 2: Direct FPS + Bitrate Presets
    {
        float dy = (1.0f - pFps) * 8.0f;
        br->SetColor(StudioTheme::color(pal.ink_subtle, pFps));
        rt->DrawTextW(L"FRAME RATE", 10, g_fmtMicro, D2D1::RectF(24.0f, 302.0f + dy, 152.0f, 317.0f + dy), br);
        rt->DrawTextW(L"BITRATE PRESET", 14, g_fmtMicro, D2D1::RectF(164.0f, 302.0f + dy, 456.0f, 317.0f + dy), br);

        static const wchar_t* fpsLabels[2] = { L"60 HZ", L"30 HZ" };
        int fpsIdx = (cfg.fps == ScreenFps::Fps60) ? 0 : 1;
        draw_segmented_bar(rt, br, pal, g_rects.fpsBar, g_rects.fpsSeg, HIT_FPS_0, 2, fpsIdx, fpsLabels, dy, pFps);

        static const wchar_t* bitLabels[5] = { L"8M", L"12M", L"16M", L"25M", L"40M" };
        int bitIdx = -1;
        for (int i = 0; i < 5; ++i) if (cfg.bitrate_mbps == BITRATE_PRESETS[i]) bitIdx = i;
        draw_segmented_bar(rt, br, pal, g_rects.bitBar, g_rects.bitSeg, HIT_BIT_0, 5, bitIdx, bitLabels, dy, pFps);
    }

    // 6. Group 3: 20-Segment Pixel Bitrate Fader (2..60 Mbps)
    {
        float dy = (1.0f - pBit) * 8.0f;
        float hv = std::max(g_hover_anim[HIT_BITRATE_FADER], g_dragging_bitrate ? 1.0f : 0.0f);
        D2D1_RECT_F vr = offset_rect(g_rects.bitBox, 0.0f, dy);

        if (hv > 0.02f) {
            D2D1_RECT_F sh = offset_rect(vr, 2.0f * hv, 2.0f * hv);
            br->SetColor(StudioTheme::color(pal.shadow_ink, pBit * hv * 0.85f));
            rt->FillRectangle(sh, br);
        }

        br->SetColor(StudioTheme::color(pal.surface, pBit));
        rt->FillRectangle(vr, br);
        draw_rect_1px(rt, br, vr, (hv > 0.25f || g_dragging_bitrate) ? pal.border_strong : pal.border, pBit);

        br->SetColor(StudioTheme::color(pal.ink, pBit));
        rt->DrawTextW(L"BITRATE", 7, g_fmtMicro, D2D1::RectF(vr.left + 12.0f, vr.top, vr.left + 92.0f, vr.bottom), br);

        constexpr int SEGMENTS = 20;
        float trackL = g_rects.bitTrack.left;
        float trackW = g_rects.bitTrack.right - g_rects.bitTrack.left;
        float segGap = 4.0f;
        float segW = std::floor((trackW - (SEGMENTS - 1) * segGap) / SEGMENTS);
        float norm = std::clamp((g_bitrate_visual - 2.0f) / 58.0f, 0.0f, 1.0f);
        int filledCount = std::max(1, (int)std::round(norm * SEGMENTS));

        for (int i = 0; i < SEGMENTS; ++i) {
            float sx = trackL + i * (segW + segGap);
            bool active = (i < filledCount);
            bool isHead = (i == filledCount - 1) && active;
            float baseH = isHead ? 22.0f : 16.0f;
            float sy = (vr.top + vr.bottom) * 0.5f - baseH * 0.5f;
            uint32_t col = active ? ((i >= 16) ? pal.accent_vermilion : pal.ink) : pal.surface_muted;
            fill_px(rt, br, sx, sy, segW, baseH, col, pBit);
            if (!active) {
                draw_rect_1px(rt, br, D2D1::RectF(sx, sy, sx + segW, sy + baseH), pal.border, pBit * 0.7f);
            }
        }

        std::wstring rateTxt = std::to_wstring(cfg.bitrate_mbps) + L" Mbps";
        br->SetColor(StudioTheme::color(pal.ink, pBit));
        rt->DrawTextW(rateTxt.c_str(), (UINT32)rateTxt.size(), g_fmtRight,
                      D2D1::RectF(382.0f, vr.top, vr.right - 12.0f, vr.bottom), br);
    }

    // 7. Group 4: Audio Routing Switches
    {
        float dy = (1.0f - pAud) * 8.0f;
        br->SetColor(StudioTheme::color(pal.ink_subtle, pAud));
        rt->DrawTextW(L"AUDIO ROUTING", 13, g_fmtMicro, D2D1::RectF(24.0f, 428.0f + dy, 200.0f, 443.0f + dy), br);

        // Live Discord isolation telemetry right-aligned
        std::wstring discMeta = L"DISCORD INCLUDED";
        uint32_t discCol = pal.ink_muted;
        if (!cfg.audio_enabled) {
            discMeta = L"AUDIO MUTED";
        } else if (cfg.exclude_discord) {
            if (g_cached_discord_pid) {
                discMeta = L"ISOLATED PID " + std::to_wstring(g_cached_discord_pid);
                discCol = pal.pixel_mint;
            } else {
                discMeta = L"DISCORD GUARD ARMED";
                discCol = pal.pixel_cyan;
            }
        }
        br->SetColor(StudioTheme::color(discCol, pAud));
        rt->DrawTextW(discMeta.c_str(), (UINT32)discMeta.size(), g_fmtRight,
                      D2D1::RectF(210.0f, 428.0f + dy, 456.0f, 443.0f + dy), br);

        draw_switch_tile(rt, br, pal, HIT_AUDIO, g_rects.audioTile, dy, pAud,
                         L"SYSTEM AUDIO", cfg.audio_enabled ? L"LOOPBACK ON" : L"MUTED",
                         cfg.audio_enabled, false, pal.pixel_mint);

        draw_switch_tile(rt, br, pal, HIT_DISCORD, g_rects.discordTile, dy, pAud,
                         L"DISCORD VOICE", cfg.audio_enabled ? (cfg.exclude_discord ? L"EXCLUDED" : L"INCLUDED") : L"DISABLED",
                         cfg.exclude_discord, !cfg.audio_enabled, pal.pixel_mint);
    }

    // 8. Group 5: Viewer Link & Server (Inline Edit + Smart Clipboard Paste)
    {
        float dy = (1.0f - pLink) * 8.0f;

        if (g_editing_url) {
            const wchar_t* editHdr = L"EDIT SERVER URL  \u00B7  ENTER SAVE  \u00B7  ESC CANCEL";
            br->SetColor(StudioTheme::color(pal.accent_vermilion, pLink));
            rt->DrawTextW(editHdr, (UINT32)wcslen(editHdr), g_fmtMicro,
                          D2D1::RectF(24.0f, 500.0f + dy, 456.0f, 517.0f + dy), br);
        } else if (!g_clip_candidate_url.empty()) {
            // Smart clipboard URL prompt right above the Viewer Link bar
            std::string clipShort = g_clip_candidate_url;
            if (clipShort.rfind("https://", 0) == 0) clipShort = clipShort.substr(8);
            else if (clipShort.rfind("http://", 0) == 0) clipShort = clipShort.substr(7);
            if (clipShort.size() > 26) clipShort = clipShort.substr(0, 24) + "..";
            std::wstring promptTxt = L"CLIPBOARD: " + to_wide(clipShort);

            fill_px(rt, br, 24.0f, 504.0f + dy, 6.0f, 6.0f, pal.pixel_mint, pLink);
            br->SetColor(StudioTheme::color(pal.ink, pLink));
            rt->DrawTextW(promptTxt.c_str(), (UINT32)promptTxt.size(), g_fmtMicro,
                          D2D1::RectF(35.0f, 498.0f + dy, 348.0f, 518.0f + dy), br);

            // [PASTE] solid mint chip + [X] dismiss chip
            {
                D2D1_RECT_F pr = offset_rect(g_rects.clipPasteBtn, 0.0f, dy);
                float hv = g_hover_anim[HIT_CLIP_PASTE];
                br->SetColor(StudioTheme::color(hv > 0.35f ? pal.ink : pal.pixel_mint, pLink));
                rt->FillRectangle(pr, br);
                draw_rect_1px(rt, br, pr, hv > 0.35f ? pal.ink : pal.pixel_mint, pLink);
                br->SetColor(StudioTheme::color(hv > 0.35f ? pal.ink_on_active : 0x0E0F12, pLink));
                rt->DrawTextW(L"PASTE URL", 9, g_fmtTag, pr, br);
            }
            {
                D2D1_RECT_F dr = offset_rect(g_rects.clipDismissBtn, 0.0f, dy);
                float hv = g_hover_anim[HIT_CLIP_DISMISS];
                br->SetColor(StudioTheme::color(hv > 0.35f ? pal.accent_vermilion : pal.surface, pLink));
                rt->FillRectangle(dr, br);
                draw_rect_1px(rt, br, dr, hv > 0.35f ? pal.accent_vermilion : pal.border, pLink);
                br->SetColor(StudioTheme::color(hv > 0.35f ? 0xFFFFFF : pal.ink_muted, pLink));
                rt->DrawTextW(L"\u00D7", 1, g_fmtTag, dr, br);
            }
        } else {
            br->SetColor(StudioTheme::color(g_saved_url_feedback ? pal.pixel_mint : pal.ink_subtle, pLink));
            const wchar_t* linkLbl = g_saved_url_feedback ? L"VIEWER LINK \u00B7 UPDATED" : L"VIEWER LINK";
            rt->DrawTextW(linkLbl, (UINT32)wcslen(linkLbl), g_fmtMicro,
                          D2D1::RectF(24.0f, 500.0f + dy, 210.0f, 517.0f + dy), br);

            // Server edit link right-aligned
            std::string hostShort = cfg.server_url;
            if (hostShort.rfind("https://", 0) == 0) hostShort = hostShort.substr(8);
            else if (hostShort.rfind("http://", 0) == 0) hostShort = hostShort.substr(7);
            while (!hostShort.empty() && hostShort.back() == '/') hostShort.pop_back();
            if (hostShort.size() > 22) hostShort = hostShort.substr(0, 20) + "..";
            std::wstring srvTxt = to_wide(hostShort) + L"  [EDIT]";
            bool srvHov = (g_hover_anim[HIT_SERVER] > 0.2f || g_hover_anim[HIT_URL_WELL] > 0.2f);
            br->SetColor(StudioTheme::color(srvHov ? pal.accent_vermilion : pal.ink_muted, pLink));
            rt->DrawTextW(srvTxt.c_str(), (UINT32)srvTxt.size(), g_fmtRight,
                          offset_rect(g_rects.serverBtn, 0.0f, dy), br);
        }

        // URL well (clickable to open inline editor!)
        D2D1_RECT_F well = offset_rect(g_rects.urlWell, 0.0f, dy);
        bool wellHov = (g_hover_anim[HIT_URL_WELL] > 0.2f);
        uint32_t wellBorder = g_editing_url
            ? pal.accent_vermilion
            : (g_saved_url_feedback ? pal.pixel_mint : (wellHov ? pal.border_strong : pal.border));
        br->SetColor(StudioTheme::color(pal.surface, pLink));
        rt->FillRectangle(well, br);
        draw_rect_1px(rt, br, well, wellBorder, pLink, g_editing_url ? 2.0f : 1.0f);

        if (!g_editing_url) {
            std::string viewUrl = cfg.server_url;
            while (!viewUrl.empty() && viewUrl.back() == '/') viewUrl.pop_back();
            viewUrl += "/r/" + cfg.room_id;
            std::string dispUrl = viewUrl;
            if (dispUrl.rfind("https://", 0) == 0) dispUrl = dispUrl.substr(8);
            else if (dispUrl.rfind("http://", 0) == 0) dispUrl = dispUrl.substr(7);
            if (dispUrl.size() > 31) dispUrl = ".." + dispUrl.substr(dispUrl.size() - 29);
            br->SetColor(StudioTheme::color(pal.ink, pLink));
            rt->DrawTextW(to_wide(dispUrl).c_str(), (UINT32)dispUrl.size(), g_fmtUrl,
                          D2D1::RectF(well.left + 10.0f, well.top, well.right - 8.0f, well.bottom), br);
        }

        if (g_editing_url) {
            draw_action_chip(rt, br, pal, HIT_COPY, g_rects.copyBtn, dy, pLink, L"SAVE", true);
            draw_action_chip(rt, br, pal, HIT_NEW_ROOM, g_rects.newRoomBtn, dy, pLink, L"PASTE", false);
            draw_action_chip(rt, br, pal, HIT_OPEN_WEB, g_rects.openWebBtn, dy, pLink, L"ESC", false);
        } else {
            draw_action_chip(rt, br, pal, HIT_COPY, g_rects.copyBtn, dy, pLink,
                             g_copied_feedback ? L"COPIED" : L"COPY", g_copied_feedback);
            draw_action_chip(rt, br, pal, HIT_NEW_ROOM, g_rects.newRoomBtn, dy, pLink, L"NEW", false);
            draw_action_chip(rt, br, pal, HIT_OPEN_WEB, g_rects.openWebBtn, dy, pLink, L"OPEN", false);
        }
    }

    // 9. Group 6: Bold Primary Action CTA (START SHARING / STOP SHARING)
    {
        float dy = (1.0f - pCta) * 8.0f;
        float hv = g_hover_anim[HIT_START_STOP];
        float pr = g_press_anim[HIT_START_STOP];
        float lift = -2.0f * hv + 2.5f * pr;

        D2D1_RECT_F r = offset_rect(g_rects.startStop, lift, dy + lift);
        if (hv > 0.02f) {
            D2D1_RECT_F sh = offset_rect(g_rects.startStop, 4.0f * hv, dy + 4.0f * hv);
            br->SetColor(StudioTheme::color(st.is_streaming ? pal.shadow_ink : pal.accent_vermilion, pCta * hv));
            rt->FillRectangle(sh, br);
        }

        uint32_t bg = st.is_streaming ? pal.accent_vermilion : pal.ink;
        uint32_t fg = st.is_streaming ? 0xFFFFFF : pal.ink_on_active;
        br->SetColor(StudioTheme::color(bg, pCta));
        rt->FillRectangle(r, br);

        fill_px(rt, br, r.left + 16.0f, (r.top + r.bottom) * 0.5f - 3.0f, 6.0f, 6.0f,
                st.is_streaming ? 0xFFFFFF : pal.pixel_mint, pCta);

        const wchar_t* ctaLbl = st.is_streaming ? L"STOP SHARING" : L"START SHARING  >";
        br->SetColor(StudioTheme::color(fg, pCta));
        rt->DrawTextW(ctaLbl, (UINT32)wcslen(ctaLbl), g_fmtCta, r, br);

        if (g_focus_id == HIT_START_STOP) {
            draw_rect_1px(rt, br, D2D1::RectF(r.left - 2.0f, r.top - 2.0f, r.right + 2.0f, r.bottom + 2.0f), pal.accent_vermilion, pCta);
        }
    }

    // 10. Architectural Footer Colophon (y: 652..704)
    br->SetColor(StudioTheme::color(pal.surface));
    rt->FillRectangle(D2D1::RectF(0.0f, 652.0f, LOGICAL_W, LOGICAL_H), br);
    br->SetColor(StudioTheme::color(pal.border));
    rt->DrawLine(D2D1::Point2F(0.0f, 652.5f), D2D1::Point2F(LOGICAL_W, 652.5f), br, 1.0f);

    std::wstring footL = g_pending_apply
        ? L"APPLYING STREAM SETTINGS\u2026"
        : (g_editing_url
            ? L"ENTER SAVE \u00B7 CTRL+V PASTE \u00B7 ESC CANCEL"
            : L"SPACE START \u00B7 CTRL+V PASTE URL \u00B7 T THEME");
    br->SetColor(StudioTheme::color(g_pending_apply ? pal.accent_vermilion : pal.ink_muted));
    rt->DrawTextW(footL.c_str(), (UINT32)footL.size(), g_fmtFooter,
                  D2D1::RectF(24.0f, 652.0f, 360.0f, LOGICAL_H), br);

    br->SetColor(StudioTheme::color(pal.ink_subtle));
    rt->DrawTextW(L"WHIP / D3D11", 12, g_fmtRight,
                  D2D1::RectF(360.0f, 652.0f, 456.0f, LOGICAL_H), br);

    draw_rect_1px(rt, br, D2D1::RectF(0.0f, 0.0f, LOGICAL_W, LOGICAL_H),
                  cfg.dark_mode ? pal.border : pal.ink);
}

static void render_ui(HWND hwnd) {
    if (FAILED(init_d2d(hwnd))) return;
    ShareConfig cfg;
    ShareState st;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        cfg = g_cfg;
        st = g_state;
    }
    draw_scene(g_rt, g_br, cfg, st);
    if (g_rt->EndDraw() == D2DERR_RECREATE_TARGET) {
        discard_d2d_targets();
    }
}

static void render_ui_to_dc(HWND hwnd, HDC hdc) {
    if (FAILED(init_d2d(hwnd)) || !g_d2d) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = std::max(1, (int)(rc.right - rc.left));
    int h = std::max(1, (int)(rc.bottom - rc.top));

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP dib = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!memDC || !dib) {
        if (dib) DeleteObject(dib);
        if (memDC) DeleteDC(memDC);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(memDC, dib);

    float dpi = get_window_dpi(hwnd);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        dpi, dpi);
    ID2D1DCRenderTarget* dcRt = nullptr;
    if (SUCCEEDED(g_d2d->CreateDCRenderTarget(&props, &dcRt)) && dcRt) {
        if (SUCCEEDED(dcRt->BindDC(memDC, &rc))) {
            dcRt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
            ID2D1SolidColorBrush* br = nullptr;
            if (SUCCEEDED(dcRt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::PALETTE_LIGHT.ink), &br)) && br) {
                ShareConfig cfg;
                ShareState st;
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    cfg = g_cfg;
                    st = g_state;
                }
                draw_scene(dcRt, br, cfg, st);
                dcRt->EndDraw();
                br->Release();
            }
        }
        dcRt->Release();
    }

    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);
}

// ============================================================================
// Animation & Interaction Helpers
// ============================================================================
static void ensure_anim_timer(HWND hwnd) {
    if (!g_anim_running && !g_window_minimized && IsWindowVisible(hwnd)) {
        QueryPerformanceCounter(&g_qpc_prev);
        SetTimer(hwnd, TIMER_ANIM, 16, nullptr);
        g_anim_running = true;
    }
}

static void stop_anim_timer(HWND hwnd) {
    if (g_anim_running) {
        KillTimer(hwnd, TIMER_ANIM);
        g_anim_running = false;
    }
}

static void tick_animation(HWND hwnd) {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    float dt = static_cast<float>(now.QuadPart - g_qpc_prev.QuadPart) /
               static_cast<float>(g_qpc_freq.QuadPart);
    g_qpc_prev = now;
    dt = std::clamp(dt, 0.001f, 0.05f);

    bool motionOn = true;
    float targetBit = 16.0f;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        motionOn = g_cfg.motion_enabled;
        targetBit = static_cast<float>(g_cfg.bitrate_mbps);
    }

    bool anyActive = false;
    if (g_intro_t < 0.70f) {
        g_intro_t += dt;
        anyActive = true;
    }

    const float hoverRate = dt / 0.16f;
    const float pressRate = dt / 0.09f;
    for (int i = 1; i < HIT_COUNT; ++i) {
        float th = (g_hover_id == i) ? 1.0f : 0.0f;
        float tp = (g_pressed_id == i) ? 1.0f : 0.0f;
        if (std::abs(g_hover_anim[i] - th) > 0.005f) {
            g_hover_anim[i] += std::clamp(th - g_hover_anim[i], -hoverRate, hoverRate);
            anyActive = true;
        } else g_hover_anim[i] = th;
        if (std::abs(g_press_anim[i] - tp) > 0.005f) {
            g_press_anim[i] += std::clamp(tp - g_press_anim[i], -pressRate, pressRate);
            anyActive = true;
        } else g_press_anim[i] = tp;
    }

    if (std::abs(g_bitrate_visual - targetBit) > 0.05f) {
        g_bitrate_visual += (targetBit - g_bitrate_visual) * std::min(1.0f, dt * 18.0f);
        anyActive = true;
    } else {
        g_bitrate_visual = targetBit;
    }

    if (motionOn) {
        g_time += dt;
        anyActive = true;
    }

    InvalidateRect(hwnd, nullptr, FALSE);
    if (!motionOn && !anyActive) stop_anim_timer(hwnd);
}

static int bitrate_from_x(float dipX) {
    float t = (dipX - g_rects.bitTrack.left) / std::max(1.0f, g_rects.bitTrack.right - g_rects.bitTrack.left);
    t = std::clamp(t, 0.0f, 1.0f);
    int raw = 2 + static_cast<int>(t * 58.0f + 0.5f);
    return std::clamp(((raw + 1) / 2) * 2, 2, 60);
}

static void schedule_live_apply(HWND hwnd) {
    bool live = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        live = g_state.is_streaming;
    }
    if (live) {
        g_pending_apply = true;
        SetTimer(hwnd, TIMER_APPLY_CFG, 420, nullptr);
    }
    ensure_anim_timer(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void update_edit_control_theme(HWND hwnd);
static void cancel_inline_edit(HWND hwnd);
static void commit_inline_edit(HWND hwnd);

static void toggle_dark_mode(HWND hwnd) {
    bool isDark = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_cfg.dark_mode = !g_cfg.dark_mode;
        isDark = g_cfg.dark_mode;
    }
    BOOL attr = isDark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &attr, sizeof(attr));
    update_edit_control_theme(hwnd);
    ensure_anim_timer(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void copy_room_link(HWND hwnd) {
    std::string view_url = g_cfg.server_url;
    while (!view_url.empty() && view_url.back() == '/') view_url.pop_back();
    view_url += "/r/" + g_cfg.room_id;
    copy_text_to_clipboard(hwnd, view_url);
    g_copied_feedback = true;
    g_copied_tick = GetTickCount();
    InvalidateRect(hwnd, nullptr, FALSE);
}

static std::string read_clipboard_utf8(HWND hwnd) {
    std::string clip_text;
    if (!OpenClipboard(hwnd)) return clip_text;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        wchar_t* pText = (wchar_t*)GlobalLock(hData);
        if (pText) {
            clip_text = to_utf8(std::wstring(pText));
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return clip_text;
}

static bool is_valid_room_token(const std::string& r) {
    if (r.size() < 2 || r.size() > 48) return false;
    for (unsigned char c : r) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

static std::string normalize_server_url(const std::string& raw, bool allow_implicit_scheme, std::string* out_room = nullptr) {
    if (out_room) out_room->clear();
    std::string s = raw;
    auto is_trim = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '\'' || c == '<' || c == '>';
    };
    while (!s.empty() && is_trim((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && is_trim((unsigned char)s.back())) s.pop_back();
    if (s.empty() || s.size() > 260) return "";

    for (unsigned char c : s) {
        if (c <= 32 || c >= 127) return "";
    }

    bool has_scheme = (s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0);
    if (!has_scheme) {
        if (!allow_implicit_scheme) return "";
        bool local = (s.rfind("localhost", 0) == 0 || s.rfind("127.", 0) == 0 ||
                      s.rfind("192.168.", 0) == 0 || s.rfind("10.", 0) == 0);
        s = std::string(local ? "http://" : "https://") + s;
    }

    size_t scheme_end = s.find("://");
    if (scheme_end == std::string::npos) return "";
    size_t host_start = scheme_end + 3;
    if (host_start >= s.size()) return "";

    // Strip query string / fragment first
    size_t qpos = s.find_first_of("?#", host_start);
    if (qpos != std::string::npos) s = s.substr(0, qpos);

    // Extract optional /r/{room} and strip trailing viewer/whip/whep route
    size_t slash = s.find('/', host_start);
    if (slash != std::string::npos) {
        std::string path = s.substr(slash);
        if (path.rfind("/r/", 0) == 0) {
            std::string room = path.substr(3);
            size_t next_s = room.find('/');
            if (next_s != std::string::npos) room = room.substr(0, next_s);
            if (out_room && is_valid_room_token(room)) *out_room = room;
            s = s.substr(0, slash);
        } else {
            // Strip trailing slashes or /whip /whep suffix
            while (!s.empty() && s.back() == '/') s.pop_back();
            auto strip_suffix = [&](const char* suf) {
                size_t len = strlen(suf);
                if (s.size() > host_start + len && s.compare(s.size() - len, len, suf) == 0) {
                    s.erase(s.size() - len);
                }
            };
            strip_suffix("/whip");
            strip_suffix("/whep");
        }
    }

    while (!s.empty() && s.back() == '/') s.pop_back();
    if (s.size() <= host_start) return "";
    std::string host = s.substr(host_start);
    size_t path_pos = host.find('/');
    if (path_pos != std::string::npos) host = host.substr(0, path_pos);
    if (host.empty() || host.find('.') == std::string::npos && host.rfind("localhost", 0) != 0) {
        if (host.find(':') == std::string::npos) return "";
    }
    return s;
}

static bool apply_server_url(HWND hwnd, const std::string& raw, const std::string& room_override, bool allow_implicit_scheme) {
    std::string parsed_room;
    std::string norm = normalize_server_url(raw, allow_implicit_scheme, &parsed_room);
    if (norm.empty()) return false;

    const std::string& room_to_use = !room_override.empty() ? room_override : parsed_room;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_cfg.server_url = norm;
        if (!room_to_use.empty()) g_cfg.room_id = room_to_use;
    }
    save_config();
    g_clip_candidate_url.clear();
    g_clip_candidate_room.clear();
    g_dismissed_clip_url.clear();
    g_saved_url_feedback = true;
    g_saved_url_tick = GetTickCount();
    schedule_live_apply(hwnd);
    return true;
}

static void check_clipboard_for_url(HWND hwnd) {
    std::string raw = read_clipboard_utf8(hwnd);
    if (raw.empty()) return;

    std::string clip_room;
    std::string norm = normalize_server_url(raw, false, &clip_room);

    std::string cur_norm;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        cur_norm = normalize_server_url(g_cfg.server_url, true);
    }

    if (!norm.empty() && norm != cur_norm && norm != g_dismissed_clip_url) {
        if (g_clip_candidate_url != norm || g_clip_candidate_room != clip_room) {
            g_clip_candidate_url = norm;
            g_clip_candidate_room = clip_room;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else if (!g_clip_candidate_url.empty() && (norm.empty() || norm == cur_norm)) {
        g_clip_candidate_url.clear();
        g_clip_candidate_room.clear();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static COLORREF hex_to_colorref(uint32_t rgb) {
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

static void update_edit_control_theme(HWND hwnd) {
    if (g_edit_bg_brush) { DeleteObject(g_edit_bg_brush); g_edit_bg_brush = nullptr; }
    bool isDark = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        isDark = g_cfg.dark_mode;
    }
    const auto& pal = StudioTheme::palette(isDark);
    g_edit_bg_brush = CreateSolidBrush(hex_to_colorref(pal.surface));

    if (g_edit_hwnd) {
        if (g_edit_font) { DeleteObject(g_edit_font); g_edit_font = nullptr; }
        int fh = -dip_to_px(hwnd, 13.5f);
        g_edit_font = CreateFontW(
            fh, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN, g_fonts.monoFamily.c_str());
        SendMessageW(g_edit_hwnd, WM_SETFONT, (WPARAM)g_edit_font, TRUE);

        int ex = dip_to_px(hwnd, g_rects.urlWell.left + 8.0f);
        int ey = dip_to_px(hwnd, g_rects.urlWell.top + 10.0f);
        int ew = dip_to_px(hwnd, (g_rects.urlWell.right - g_rects.urlWell.left) - 16.0f);
        int eh = dip_to_px(hwnd, (g_rects.urlWell.bottom - g_rects.urlWell.top) - 18.0f);
        SetWindowPos(g_edit_hwnd, HWND_TOP, ex, ey, ew, eh, SWP_NOACTIVATE);
        InvalidateRect(g_edit_hwnd, nullptr, TRUE);
    }
}

static LRESULT CALLBACK EditSubclassProc(HWND hEdit, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            commit_inline_edit(GetParent(hEdit));
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            cancel_inline_edit(GetParent(hEdit));
            return 0;
        }
        if ((GetKeyState(VK_CONTROL) & 0x8000) && wParam == 'A') {
            SendMessageW(hEdit, EM_SETSEL, 0, -1);
            return 0;
        }
    } else if (msg == WM_CHAR) {
        if (wParam == VK_RETURN || wParam == VK_ESCAPE) return 0; // Suppress system beep
    }
    return CallWindowProcW(g_edit_orig_proc, hEdit, msg, wParam, lParam);
}

static void destroy_inline_edit_ctrl() {
    if (g_edit_hwnd) {
        if (g_edit_orig_proc) {
            SetWindowLongPtrW(g_edit_hwnd, GWLP_WNDPROC, (LONG_PTR)g_edit_orig_proc);
            g_edit_orig_proc = nullptr;
        }
        DestroyWindow(g_edit_hwnd);
        g_edit_hwnd = nullptr;
    }
    if (g_edit_font) { DeleteObject(g_edit_font); g_edit_font = nullptr; }
    if (g_edit_bg_brush) { DeleteObject(g_edit_bg_brush); g_edit_bg_brush = nullptr; }
    g_editing_url = false;
}

static void begin_inline_edit(HWND hwnd, const std::string* prefill = nullptr) {
    if (g_editing_url && g_edit_hwnd) {
        if (prefill) {
            std::wstring w = to_wide(*prefill);
            SetWindowTextW(g_edit_hwnd, w.c_str());
        }
        SetFocus(g_edit_hwnd);
        SendMessageW(g_edit_hwnd, EM_SETSEL, 0, -1);
        return;
    }

    int ex = dip_to_px(hwnd, g_rects.urlWell.left + 8.0f);
    int ey = dip_to_px(hwnd, g_rects.urlWell.top + 10.0f);
    int ew = dip_to_px(hwnd, (g_rects.urlWell.right - g_rects.urlWell.left) - 16.0f);
    int eh = dip_to_px(hwnd, (g_rects.urlWell.bottom - g_rects.urlWell.top) - 18.0f);

    std::string init_text;
    if (prefill) {
        init_text = *prefill;
    } else {
        std::lock_guard<std::mutex> lk(g_mu);
        init_text = g_cfg.server_url;
    }

    g_edit_hwnd = CreateWindowExW(
        0, L"EDIT", to_wide(init_text).c_str(),
        WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
        ex, ey, ew, eh,
        hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!g_edit_hwnd) return;
    g_editing_url = true;
    g_edit_orig_proc = (WNDPROC)SetWindowLongPtrW(g_edit_hwnd, GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
    update_edit_control_theme(hwnd);
    SetFocus(g_edit_hwnd);
    SendMessageW(g_edit_hwnd, EM_SETSEL, 0, -1);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void commit_inline_edit(HWND hwnd) {
    if (!g_editing_url || !g_edit_hwnd) return;
    wchar_t buf[512] = {};
    GetWindowTextW(g_edit_hwnd, buf, 512);
    std::string entered = to_utf8(std::wstring(buf));
    destroy_inline_edit_ctrl();
    apply_server_url(hwnd, entered, "", true);
    SetFocus(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void cancel_inline_edit(HWND hwnd) {
    if (!g_editing_url) return;
    destroy_inline_edit_ctrl();
    SetFocus(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void paste_into_inline_edit(HWND hwnd) {
    std::string clip = read_clipboard_utf8(hwnd);
    if (clip.empty()) return;
    std::string clip_room;
    std::string norm = normalize_server_url(clip, true, &clip_room);
    if (g_editing_url && g_edit_hwnd) {
        std::string txt = norm.empty() ? clip : norm;
        SetWindowTextW(g_edit_hwnd, to_wide(txt).c_str());
        SetFocus(g_edit_hwnd);
        SendMessageW(g_edit_hwnd, EM_SETSEL, 0, -1);
    } else if (!norm.empty()) {
        apply_server_url(hwnd, norm, clip_room, true);
    } else {
        begin_inline_edit(hwnd, &clip);
    }
}

static void activate_hit(HWND hwnd, int h) {
    if (g_editing_url) {
        if (h == HIT_COPY) {
            commit_inline_edit(hwnd);
            return;
        }
        if (h == HIT_NEW_ROOM) {
            paste_into_inline_edit(hwnd);
            return;
        }
        if (h == HIT_OPEN_WEB) {
            cancel_inline_edit(hwnd);
            return;
        }
        if (h == HIT_URL_WELL) {
            if (g_edit_hwnd) SetFocus(g_edit_hwnd);
            return;
        }
        // Clicking any other control while editing commits the URL first
        commit_inline_edit(hwnd);
    }

    if (h == HIT_CLOSE) {
        DestroyWindow(hwnd);
    } else if (h == HIT_MIN) {
        ShowWindow(hwnd, SW_MINIMIZE);
    } else if (h == HIT_THEME) {
        toggle_dark_mode(hwnd);
    } else if (h == HIT_MOTION) {
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_cfg.motion_enabled = !g_cfg.motion_enabled;
        }
        ensure_anim_timer(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (h >= HIT_RES_0 && h <= HIT_RES_3) {
        g_cfg.resolution = static_cast<ScreenResolution>(h - HIT_RES_0);
        schedule_live_apply(hwnd);
    } else if (h == HIT_FPS_0 || h == HIT_FPS_1) {
        g_cfg.fps = (h == HIT_FPS_0) ? ScreenFps::Fps60 : ScreenFps::Fps30;
        schedule_live_apply(hwnd);
    } else if (h >= HIT_BIT_0 && h <= HIT_BIT_4) {
        g_cfg.bitrate_mbps = BITRATE_PRESETS[h - HIT_BIT_0];
        schedule_live_apply(hwnd);
    } else if (h == HIT_AUDIO) {
        g_cfg.audio_enabled = !g_cfg.audio_enabled;
        schedule_live_apply(hwnd);
    } else if (h == HIT_DISCORD) {
        if (g_cfg.audio_enabled) {
            g_cfg.exclude_discord = !g_cfg.exclude_discord;
            g_cached_discord_pid = g_cfg.exclude_discord ? find_discord_root_pid() : 0;
            schedule_live_apply(hwnd);
        }
    } else if (h == HIT_CLIP_PASTE) {
        if (!g_clip_candidate_url.empty()) {
            apply_server_url(hwnd, g_clip_candidate_url, g_clip_candidate_room, true);
        }
    } else if (h == HIT_CLIP_DISMISS) {
        g_dismissed_clip_url = g_clip_candidate_url;
        g_clip_candidate_url.clear();
        g_clip_candidate_room.clear();
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (h == HIT_SERVER || h == HIT_URL_WELL) {
        begin_inline_edit(hwnd);
    } else if (h == HIT_COPY) {
        copy_room_link(hwnd);
    } else if (h == HIT_NEW_ROOM) {
        g_cfg.room_id = generate_random_room();
        schedule_live_apply(hwnd);
    } else if (h == HIT_OPEN_WEB) {
        std::string view_url = g_cfg.server_url;
        while (!view_url.empty() && view_url.back() == '/') view_url.pop_back();
        view_url += "/r/" + g_cfg.room_id;
        ShellExecuteW(nullptr, L"open", to_wide(view_url).c_str(), nullptr, nullptr, SW_SHOW);
    } else if (h == HIT_START_STOP) {
        KillTimer(hwnd, TIMER_APPLY_CFG);
        g_pending_apply = false;
        if (g_state.is_streaming) stop_stream_process();
        else start_stream_process();
    }
}

static const int FOCUS_ORDER[] = {
    HIT_RES_0, HIT_RES_1, HIT_RES_2, HIT_RES_3,
    HIT_FPS_0, HIT_FPS_1,
    HIT_BIT_0, HIT_BIT_1, HIT_BIT_2, HIT_BIT_3, HIT_BIT_4,
    HIT_AUDIO, HIT_DISCORD,
    HIT_COPY, HIT_NEW_ROOM, HIT_OPEN_WEB,
    HIT_START_STOP
};

// ============================================================================
// Win32 Window Procedure
// ============================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        BOOL dark = FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        DWORD corner = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        if (g_appIcon) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_appIcon);
        if (g_appIconSmall) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_appIconSmall);

        BOOL animAllowed = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animAllowed, 0);
        g_cfg.motion_enabled = (animAllowed != FALSE);

        g_cached_discord_pid = g_cfg.exclude_discord ? find_discord_root_pid() : 0;
        g_discord_check_tick = GetTickCount();

        QueryPerformanceFrequency(&g_qpc_freq);
        QueryPerformanceCounter(&g_qpc_prev);
        AddClipboardFormatListener(hwnd);
        check_clipboard_for_url(hwnd);
        ensure_anim_timer(hwnd);
        SetTimer(hwnd, TIMER_CLOCK, 1000, nullptr);
        return 0;
    }
    case WM_CLIPBOARDUPDATE: {
        check_clipboard_for_url(hwnd);
        return 0;
    }
    case WM_ACTIVATE: {
        if (LOWORD(wParam) != WA_INACTIVE) {
            check_clipboard_for_url(hwnd);
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        bool isDark = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            isDark = g_cfg.dark_mode;
        }
        const auto& pal = StudioTheme::palette(isDark);
        SetTextColor(hdc, hex_to_colorref(pal.ink));
        SetBkColor(hdc, hex_to_colorref(pal.surface));
        if (!g_edit_bg_brush) g_edit_bg_brush = CreateSolidBrush(hex_to_colorref(pal.surface));
        return reinterpret_cast<LRESULT>(g_edit_bg_brush);
    }
    case WM_STREAM_STATE: {
        ensure_anim_timer(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER: {
        if (wParam == TIMER_ANIM) {
            tick_animation(hwnd);
        } else if (wParam == TIMER_APPLY_CFG) {
            KillTimer(hwnd, TIMER_APPLY_CFG);
            g_pending_apply = false;
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wParam == TIMER_CLOCK) {
            uint32_t now = GetTickCount();
            bool needPaint = false;
            if (g_copied_feedback && (now - g_copied_tick > 2200)) {
                g_copied_feedback = false;
                needPaint = true;
            }
            if (g_saved_url_feedback && (now - g_saved_url_tick > 2200)) {
                g_saved_url_feedback = false;
                needPaint = true;
            }
            if (g_cfg.exclude_discord && (now - g_discord_check_tick > 3000)) {
                DWORD pid = find_discord_root_pid();
                g_discord_check_tick = now;
                if (pid != g_cached_discord_pid) {
                    g_cached_discord_pid = pid;
                    needPaint = true;
                }
            }
            if (g_state.is_streaming) needPaint = true;
            if (needPaint) InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_SIZE: {
        if (wParam == SIZE_MINIMIZED) {
            g_window_minimized = true;
            stop_anim_timer(hwnd);
        } else {
            g_window_minimized = false;
            if (g_rt) {
                g_rt->Resize(D2D1::SizeU(std::max<UINT>(1, LOWORD(lParam)), std::max<UINT>(1, HIWORD(lParam))));
            }
            ensure_anim_timer(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* s = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, s->left, s->top, s->right - s->left, s->bottom - s->top, SWP_NOZORDER | SWP_NOACTIVATE);
        discard_d2d_targets();
        if (g_editing_url) update_edit_control_theme(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        render_ui(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINT:
    case WM_PRINTCLIENT: {
        render_ui_to_dc(hwnd, reinterpret_cast<HDC>(wParam));
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (hit_test_ui(px_to_dip(hwnd, pt.x), px_to_dip(hwnd, pt.y)) != HIT_NONE) {
            return HTCLIENT;
        }
        return HTCAPTION;
    }
    case WM_NCLBUTTONDOWN: {
        if (g_editing_url) commit_inline_edit(hwnd);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_LBUTTONDOWN: {
        float x = px_to_dip(hwnd, GET_X_LPARAM(lParam));
        float y = px_to_dip(hwnd, GET_Y_LPARAM(lParam));
        g_focus_id = HIT_NONE;
        int h = hit_test_ui(x, y);
        g_pressed_id = h;
        ensure_anim_timer(hwnd);
        if (h == HIT_BITRATE_FADER) {
            g_dragging_bitrate = true;
            g_drag_start_bitrate = g_cfg.bitrate_mbps;
            g_cfg.bitrate_mbps = bitrate_from_x(x);
            SetCapture(hwnd);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE: {
        float x = px_to_dip(hwnd, GET_X_LPARAM(lParam));
        float y = px_to_dip(hwnd, GET_Y_LPARAM(lParam));
        g_mouse_x = x;
        g_mouse_y = y;

        if (!g_mouse_inside) {
            g_mouse_inside = true;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
        }
        if (g_dragging_bitrate) {
            int nextBit = bitrate_from_x(x);
            if (nextBit != g_cfg.bitrate_mbps) {
                g_cfg.bitrate_mbps = nextBit;
                ensure_anim_timer(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        int h = hit_test_ui(x, y);
        if (h != g_hover_id) {
            g_hover_id = h;
            ensure_anim_timer(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        SetCursor(LoadCursorW(nullptr, h == HIT_URL_WELL ? IDC_IBEAM : (h != HIT_NONE ? IDC_HAND : IDC_ARROW)));
        return 0;
    }
    case WM_MOUSELEAVE: {
        g_mouse_inside = false;
        if (g_hover_id != HIT_NONE) {
            g_hover_id = HIT_NONE;
            ensure_anim_timer(hwnd);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        float x = px_to_dip(hwnd, GET_X_LPARAM(lParam));
        float y = px_to_dip(hwnd, GET_Y_LPARAM(lParam));
        g_pressed_id = HIT_NONE;
        ensure_anim_timer(hwnd);
        if (g_dragging_bitrate) {
            g_dragging_bitrate = false;
            ReleaseCapture();
            if (g_cfg.bitrate_mbps != g_drag_start_bitrate) {
                schedule_live_apply(hwnd);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        int h = hit_test_ui(x, y);
        if (h != HIT_NONE) activate_hit(hwnd, h);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int step = (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 2 : -2;
        int nextBit = std::clamp(g_cfg.bitrate_mbps + step, 2, 60);
        if (nextBit != g_cfg.bitrate_mbps) {
            g_cfg.bitrate_mbps = nextBit;
            schedule_live_apply(hwnd);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl && wParam == 'C') { copy_room_link(hwnd); return 0; }
        if (ctrl && wParam == 'V') { paste_into_inline_edit(hwnd); return 0; }
        if (wParam == VK_SPACE) { activate_hit(hwnd, HIT_START_STOP); return 0; }
        if (wParam == 'T') { toggle_dark_mode(hwnd); return 0; }
        if (wParam == 'M') { activate_hit(hwnd, HIT_AUDIO); return 0; }
        if (wParam == VK_ESCAPE) { ShowWindow(hwnd, SW_MINIMIZE); return 0; }
        if (wParam == VK_RETURN && g_focus_id != HIT_NONE) { activate_hit(hwnd, g_focus_id); return 0; }
        if (wParam == VK_TAB) {
            int n = (int)(sizeof(FOCUS_ORDER) / sizeof(FOCUS_ORDER[0]));
            int idx = -1;
            for (int i = 0; i < n; ++i) if (FOCUS_ORDER[i] == g_focus_id) { idx = i; break; }
            idx = (idx < 0) ? (shift ? n - 1 : 0) : ((idx + (shift ? -1 : 1) + n) % n);
            g_focus_id = FOCUS_ORDER[idx];
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_LEFT || wParam == VK_RIGHT) {
            int step = (wParam == VK_RIGHT) ? 2 : -2;
            g_cfg.bitrate_mbps = std::clamp(g_cfg.bitrate_mbps + step, 2, 60);
            schedule_live_apply(hwnd);
            return 0;
        }
        if (wParam >= '1' && wParam <= '4') {
            g_cfg.resolution = static_cast<ScreenResolution>(wParam - '1');
            schedule_live_apply(hwnd);
            return 0;
        }
        break;
    }
    case WM_DESTROY: {
        g_quitting.store(true);
        RemoveClipboardFormatListener(hwnd);
        destroy_inline_edit_ctrl();
        stop_anim_timer(hwnd);
        KillTimer(hwnd, TIMER_APPLY_CFG);
        KillTimer(hwnd, TIMER_CLOCK);
        stop_stream_process();
        release_all_d2d();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
    enable_per_monitor_dpi();
    load_config();

    if (lpCmdLine && strlen(lpCmdLine) > 0) {
        std::string arg = lpCmdLine;
        while (!arg.empty() && (arg.front() == ' ' || arg.front() == '"')) arg.erase(0, 1);
        while (!arg.empty() && (arg.back() == ' ' || arg.back() == '"')) arg.pop_back();
        if (arg.rfind("http", 0) == 0) {
            g_cfg.server_url = arg;
            save_config();
        }
    }

    g_cfg.room_id = generate_random_room();

    g_job = CreateJobObjectW(nullptr, nullptr);
    if (g_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    g_appIcon = reinterpret_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    g_appIconSmall = reinterpret_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    if (!g_appIcon) g_appIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!g_appIconSmall) g_appIconSmall = g_appIcon;

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"GhostShareJorWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = g_appIcon;
    wc.hIconSm = g_appIconSmall;
    RegisterClassExW(&wc);

    HDC screenDC = GetDC(nullptr);
    float sysDpi = screenDC ? static_cast<float>(GetDeviceCaps(screenDC, LOGPIXELSX)) : 96.0f;
    if (screenDC) ReleaseDC(nullptr, screenDC);

    int winW = (int)std::round(LOGICAL_W * sysDpi / 96.0f);
    int winH = (int)std::round(LOGICAL_H * sysDpi / 96.0f);
    int wx = (GetSystemMetrics(SM_CXSCREEN) - winW) / 2;
    int wy = (GetSystemMetrics(SM_CYSCREEN) - winH) / 2;

    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"GHOST SHARE JOR",
        WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_VISIBLE,
        wx, wy, winW, winH,
        nullptr, nullptr, hInstance, nullptr
    );

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_job) CloseHandle(g_job);
    return 0;
}
