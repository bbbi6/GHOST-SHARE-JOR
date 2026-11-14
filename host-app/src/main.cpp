#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstdio>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

#include "ghost_share_core.h"
#include "theme.h"

static const int WINDOW_WIDTH = 480;
static const int WINDOW_HEIGHT = 596;

static HWND g_hwnd = nullptr;
static ID2D1Factory* g_d2d = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static IDWriteFactory* g_dw = nullptr;
static StudioTheme::FontSet g_fonts;

static IDWriteTextFormat* g_tf_title = nullptr;
static IDWriteTextFormat* g_tf_section = nullptr;
static IDWriteTextFormat* g_tf_body = nullptr;
static IDWriteTextFormat* g_tf_body_center = nullptr;
static IDWriteTextFormat* g_tf_sub = nullptr;
static IDWriteTextFormat* g_tf_mono = nullptr;
static IDWriteTextFormat* g_tf_mono_right = nullptr;
static IDWriteTextFormat* g_tf_mono_sm = nullptr;
static IDWriteTextFormat* g_tf_mono_center = nullptr;
static IDWriteTextFormat* g_tf_hint_center = nullptr;
static IDWriteTextFormat* g_tf_btn = nullptr;

static inline void d2d_draw(ID2D1RenderTarget* rt, const std::wstring& text, IDWriteTextFormat* fmt, const D2D1_RECT_F& rc, ID2D1Brush* brush) {
    if (rt && fmt && brush && !text.empty()) {
        rt->DrawText(text.c_str(), (UINT32)text.size(), fmt, rc, brush);
    }
}

static inline void d2d_draw(ID2D1RenderTarget* rt, const wchar_t* text, IDWriteTextFormat* fmt, const D2D1_RECT_F& rc, ID2D1Brush* brush) {
    if (rt && fmt && brush && text) {
        rt->DrawText(text, (UINT32)wcslen(text), fmt, rc, brush);
    }
}

static ShareConfig g_cfg;
static ShareState g_state;
static std::mutex g_mu;
static std::atomic<bool> g_quitting{false};
static HANDLE g_stream_proc = nullptr;
static HANDLE g_job = nullptr;
static bool g_copied_feedback = false;
static uint32_t g_copied_tick = 0;

static const int BITRATE_PRESETS[5] = { 8, 12, 16, 25, 40 };

enum class HoverTarget {
    None = 0,
    Close,
    Minimize,
    Server,
    Res0, Res1, Res2, Res3,
    Fps0, Fps1,
    Bit0, Bit1, Bit2, Bit3, Bit4,
    BitrateSlider,
    Audio,
    Discord,
    Copy,
    NewRoom,
    OpenWeb,
    StartStop
};

static HoverTarget g_hover = HoverTarget::None;
static HoverTarget g_focus = HoverTarget::None;
static bool g_tracking_mouse = false;
static bool g_dragging_bitrate = false;
static int g_drag_start_bitrate = 16;

struct UiRects {
    D2D1_RECT_F btnClose;
    D2D1_RECT_F btnMinimize;
    D2D1_RECT_F boxServer;

    D2D1_RECT_F chipRes[4];
    D2D1_RECT_F chipFps[2];
    D2D1_RECT_F chipBit[5];
    D2D1_RECT_F bitrateTrack;

    D2D1_RECT_F rowAudio;
    D2D1_RECT_F rowDiscord;

    D2D1_RECT_F btnCopy;
    D2D1_RECT_F btnNewRoom;
    D2D1_RECT_F btnOpenWeb;
    D2D1_RECT_F btnStartStop;
} g_rects;

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
    if (s != std::wstring::npos) return p.substr(0, s);
    return L".";
}

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
    if (data.size() > 160) data = data.substr(data.size() - 160);
    return data;
}

static void load_config() {
    std::wstring app_dir = get_app_dir();
    std::wstring cfg_path = app_dir + L"\\server.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, cfg_path.c_str(), L"r") == 0 && f) {
        char buf[512] = {};
        if (fgets(buf, sizeof(buf), f)) {
            std::string s = buf;
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            if (!s.empty()) {
                g_cfg.server_url = s;
            }
        }
        fclose(f);
    } else {
        if (_wfopen_s(&f, cfg_path.c_str(), L"w") == 0 && f) {
            fputs("http://localhost:8889\n", f);
            fclose(f);
        }
    }
}

static void save_config() {
    std::wstring app_dir = get_app_dir();
    std::wstring cfg_path = app_dir + L"\\server.txt";
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
            if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
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
        if (hLog != INVALID_HANDLE_VALUE) {
            CloseHandle(hLog);
        }

        if (!ok) {
            std::lock_guard<std::mutex> lk(g_mu);
            g_state.is_streaming = false;
            g_state.status_text = "Error";
            g_state.error_msg = "Could not start bundled capture process.";
            if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
            return;
        }

        if (g_job) {
            AssignProcessToJobObject(g_job, pi.hProcess);
        }
        CloseHandle(pi.hThread);

        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_stream_proc = pi.hProcess;
            g_state.status_text = "Live";
        }
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);

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
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    }).detach();
}

static bool hit_test(const D2D1_RECT_F& rc, float x, float y) {
    return x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom;
}

static int bitrate_from_x(float x) {
    float left = g_rects.bitrateTrack.left + 8.0f;
    float right = g_rects.bitrateTrack.right - 8.0f;
    float t = (x - left) / (std::max)(1.0f, right - left);
    t = std::clamp(t, 0.0f, 1.0f);
    int raw = 2 + static_cast<int>(t * 58.0f + 0.5f);
    int snapped = ((raw + 1) / 2) * 2;
    return std::clamp(snapped, 2, 60);
}

static void draw_focus_outline(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, float radius, ID2D1Brush* bFocus) {
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
        D2D1::RectF(rc.left - 2.0f, rc.top - 2.0f, rc.right + 2.0f, rc.bottom + 2.0f),
        radius + 2.0f, radius + 2.0f
    );
    rt->DrawRoundedRectangle(rr, bFocus, 1.5f);
}

static void draw_toggle_switch(
    ID2D1RenderTarget* rt, float rightX, float centerY, bool on, bool disabled,
    ID2D1Brush* bAccent, ID2D1Brush* bElevated, ID2D1Brush* bBorder, ID2D1Brush* bThumb
) {
    float w = 38.0f;
    float h = 22.0f;
    D2D1_RECT_F track = D2D1::RectF(rightX - w, centerY - h * 0.5f, rightX, centerY + h * 0.5f);
    D2D1_ROUNDED_RECT rTrack = D2D1::RoundedRect(track, 11.0f, 11.0f);

    if (on && !disabled) {
        rt->FillRoundedRectangle(rTrack, bAccent);
    } else {
        rt->FillRoundedRectangle(rTrack, bElevated);
        rt->DrawRoundedRectangle(rTrack, bBorder, 1.0f);
    }

    float cx = (on && !disabled) ? (track.right - 11.0f) : (track.left + 11.0f);
    D2D1_ELLIPSE thumb = D2D1::Ellipse(D2D1::Point2F(cx, centerY), 8.0f, 8.0f);
    rt->FillEllipse(thumb, bThumb);
}

static void render_to(ID2D1RenderTarget* rt) {
    if (!rt) return;

    ShareConfig cfg;
    ShareState st;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        cfg = g_cfg;
        st = g_state;
    }

    rt->BeginDraw();
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rt->Clear(StudioTheme::color(StudioTheme::BG_CANVAS));

    ID2D1SolidColorBrush* bSurface = nullptr;
    ID2D1SolidColorBrush* bElevated = nullptr;
    ID2D1SolidColorBrush* bHover = nullptr;
    ID2D1SolidColorBrush* bActive = nullptr;
    ID2D1SolidColorBrush* bCanvas = nullptr;
    ID2D1SolidColorBrush* bText = nullptr;
    ID2D1SolidColorBrush* bTextSec = nullptr;
    ID2D1SolidColorBrush* bMuted = nullptr;
    ID2D1SolidColorBrush* bTextDark = nullptr;
    ID2D1SolidColorBrush* bBorder = nullptr;
    ID2D1SolidColorBrush* bBorderStr = nullptr;
    ID2D1SolidColorBrush* bHighlight = nullptr;
    ID2D1SolidColorBrush* bAccent = nullptr;
    ID2D1SolidColorBrush* bAccentHover = nullptr;
    ID2D1SolidColorBrush* bAccentBg = nullptr;
    ID2D1SolidColorBrush* bGreen = nullptr;
    ID2D1SolidColorBrush* bGreenBg = nullptr;
    ID2D1SolidColorBrush* bRed = nullptr;
    ID2D1SolidColorBrush* bRedBg = nullptr;
    ID2D1SolidColorBrush* bRedText = nullptr;

    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::BG_SURFACE), &bSurface);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::BG_ELEVATED), &bElevated);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::BG_HOVER), &bHover);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::BG_ACTIVE), &bActive);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::BG_CANVAS), &bCanvas);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::TEXT_PRIMARY), &bText);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::TEXT_SECONDARY), &bTextSec);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::TEXT_MUTED), &bMuted);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::TEXT_DARK), &bTextDark);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::TEXT_PRIMARY, StudioTheme::BORDER_ALPHA), &bBorder);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::TEXT_PRIMARY, StudioTheme::BORDER_STR_ALPHA), &bBorderStr);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::TEXT_PRIMARY, StudioTheme::HIGHLIGHT_ALPHA), &bHighlight);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::ACCENT), &bAccent);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::ACCENT_HOVER), &bAccentHover);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::ACCENT, StudioTheme::ACCENT_BG_ALPHA), &bAccentBg);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::LIVE_GREEN), &bGreen);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::LIVE_GREEN, StudioTheme::GREEN_BG_ALPHA), &bGreenBg);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::DANGER_RED), &bRed);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::DANGER_RED, StudioTheme::DANGER_BG_ALPHA), &bRedBg);
    rt->CreateSolidColorBrush(StudioTheme::color(StudioTheme::DANGER_TEXT), &bRedText);

    // Subtle outer border
    rt->DrawRectangle(D2D1::RectF(0.5f, 0.5f, WINDOW_WIDTH - 0.5f, WINDOW_HEIGHT - 0.5f), bBorder, 1.0f);

    // --- HEADER BAR (y: 0..56) ---
    D2D1_ROUNDED_RECT badgeRr = D2D1::RoundedRect(D2D1::RectF(20.0f, 15.0f, 44.0f, 39.0f), 6.0f, 6.0f);
    rt->FillRoundedRectangle(badgeRr, bElevated);
    rt->DrawRoundedRectangle(badgeRr, bBorderStr, 1.0f);
    D2D1_ROUNDED_RECT innerScreen = D2D1::RoundedRect(D2D1::RectF(25.0f, 21.0f, 39.0f, 31.0f), 2.0f, 2.0f);
    rt->DrawRoundedRectangle(innerScreen, bAccent, 1.4f);
    rt->DrawLine(D2D1::Point2F(29.0f, 34.0f), D2D1::Point2F(35.0f, 34.0f), bAccent, 1.4f);

    d2d_draw(rt, L"Ghost Share Jor", g_tf_title, D2D1::RectF(54.0f, 17.0f, 184.0f, 38.0f), bText);

    bool isLive = (st.is_streaming && st.status_text == "Live");
    bool isConnecting = (st.is_streaming && st.status_text == "Connecting");
    bool isError = (!st.error_msg.empty());

    std::wstring statusStr = to_wide(st.status_text.empty() ? "Standby" : st.status_text);
    if (st.is_streaming) {
        uint32_t sec = (GetTickCount() - st.start_time) / 1000;
        wchar_t tbuf[32];
        swprintf_s(tbuf, L" · %02u:%02u", sec / 60, sec % 60);
        statusStr += tbuf;
    }

    D2D1_ROUNDED_RECT statusPill = D2D1::RoundedRect(D2D1::RectF(190.0f, 17.0f, 346.0f, 39.0f), 11.0f, 11.0f);
    rt->FillRoundedRectangle(statusPill, isLive ? bGreenBg : (isError ? bRedBg : bSurface));
    rt->DrawRoundedRectangle(statusPill, isLive ? bGreen : (isError ? bRed : bBorder), 1.0f);
    ID2D1Brush* dotBrush = isLive ? bGreen : (isConnecting ? bAccent : (isError ? bRed : bMuted));
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(202.0f, 28.0f), 3.0f, 3.0f), dotBrush);
    d2d_draw(rt, statusStr, g_tf_mono_sm, D2D1::RectF(212.0f, 20.5f, 340.0f, 36.5f), isLive ? bGreen : (isError ? bRedText : bTextSec));

    g_rects.btnMinimize = D2D1::RectF(394.0f, 14.0f, 424.0f, 40.0f);
    g_rects.btnClose    = D2D1::RectF(428.0f, 14.0f, 458.0f, 40.0f);
    if (g_hover == HoverTarget::Minimize) rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.btnMinimize, 6.0f, 6.0f), bHover);
    if (g_hover == HoverTarget::Close)    rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.btnClose, 6.0f, 6.0f), bRed);
    d2d_draw(rt, L"-", g_tf_body_center, g_rects.btnMinimize, bTextSec);
    d2d_draw(rt, L"x", g_tf_body_center, g_rects.btnClose, (g_hover == HoverTarget::Close) ? bText : bTextSec);

    rt->DrawLine(D2D1::Point2F(0.0f, 56.5f), D2D1::Point2F((float)WINDOW_WIDTH, 56.5f), bBorder, 1.0f);

    // --- CARD 1: VIDEO QUALITY (x: 20..460, y: 68..276) ---
    D2D1_RECT_F c1 = D2D1::RectF(20.0f, 68.0f, 460.0f, 276.0f);
    rt->FillRoundedRectangle(D2D1::RoundedRect(c1, 12.0f, 12.0f), bSurface);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(c1, 12.0f, 12.0f), bBorder, 1.0f);

    d2d_draw(rt, L"Video Quality", g_tf_section, D2D1::RectF(36.0f, 82.0f, 170.0f, 100.0f), bText);
    std::wstring profileSummary = std::wstring(resolution_title(cfg.resolution)) + L" · " + std::to_wstring((int)cfg.fps) + L" fps · " + std::to_wstring(cfg.bitrate_mbps) + L" Mbps";
    d2d_draw(rt, profileSummary, g_tf_mono_right, D2D1::RectF(180.0f, 83.0f, 444.0f, 100.0f), bTextSec);

    D2D1_RECT_F resWell = D2D1::RectF(36.0f, 108.0f, 444.0f, 144.0f);
    rt->FillRoundedRectangle(D2D1::RoundedRect(resWell, 8.0f, 8.0f), bCanvas);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(resWell, 8.0f, 8.0f), bBorder, 1.0f);

    const wchar_t* resLabels[4] = { L"Source", L"1440p", L"1080p", L"720p" };
    float resInnerW = (444.0f - 36.0f - 6.0f) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        g_rects.chipRes[i] = D2D1::RectF(39.0f + i * resInnerW, 111.0f, 39.0f + (i + 1) * resInnerW, 141.0f);
        bool sel = ((int)cfg.resolution == i);
        bool hov = (g_hover == (HoverTarget)((int)HoverTarget::Res0 + i));
        if (sel) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.chipRes[i], 6.0f, 6.0f), bActive);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(g_rects.chipRes[i], 6.0f, 6.0f), bBorderStr, 1.0f);
        } else if (hov) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.chipRes[i], 6.0f, 6.0f), bElevated);
        }
        d2d_draw(rt, resLabels[i], g_tf_body_center, g_rects.chipRes[i], sel ? bText : (hov ? bText : bTextSec));
        if (g_focus == (HoverTarget)((int)HoverTarget::Res0 + i)) draw_focus_outline(rt, g_rects.chipRes[i], 6.0f, bAccent);
    }

    D2D1_RECT_F fpsWell = D2D1::RectF(36.0f, 156.0f, 174.0f, 190.0f);
    rt->FillRoundedRectangle(D2D1::RoundedRect(fpsWell, 8.0f, 8.0f), bCanvas);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(fpsWell, 8.0f, 8.0f), bBorder, 1.0f);

    const wchar_t* fpsLabels[2] = { L"60 fps", L"30 fps" };
    float fpsW = (174.0f - 36.0f - 6.0f) / 2.0f;
    for (int i = 0; i < 2; ++i) {
        g_rects.chipFps[i] = D2D1::RectF(39.0f + i * fpsW, 159.0f, 39.0f + (i + 1) * fpsW, 187.0f);
        bool sel = (i == 0 && cfg.fps == ScreenFps::Fps60) || (i == 1 && cfg.fps == ScreenFps::Fps30);
        bool hov = (g_hover == (i == 0 ? HoverTarget::Fps0 : HoverTarget::Fps1));
        if (sel) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.chipFps[i], 6.0f, 6.0f), bActive);
            rt->DrawRoundedRectangle(D2D1::RoundedRect(g_rects.chipFps[i], 6.0f, 6.0f), bBorderStr, 1.0f);
        } else if (hov) {
            rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.chipFps[i], 6.0f, 6.0f), bElevated);
        }
        d2d_draw(rt, fpsLabels[i], g_tf_body_center, g_rects.chipFps[i], sel ? bText : bTextSec);
        if (g_focus == (i == 0 ? HoverTarget::Fps0 : HoverTarget::Fps1)) draw_focus_outline(rt, g_rects.chipFps[i], 6.0f, bAccent);
    }

    const wchar_t* bitLabels[5] = { L"8M", L"12M", L"16M", L"25M", L"40M" };
    float bitX = 190.0f;
    float bitW = 47.0f;
    float bitGap = 4.75f;
    for (int i = 0; i < 5; ++i) {
        float x0 = bitX + i * (bitW + bitGap);
        g_rects.chipBit[i] = D2D1::RectF(x0, 156.0f, x0 + bitW, 190.0f);
        bool sel = (cfg.bitrate_mbps == BITRATE_PRESETS[i]);
        bool hov = (g_hover == (HoverTarget)((int)HoverTarget::Bit0 + i));
        rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.chipBit[i], 7.0f, 7.0f), sel ? bAccentBg : (hov ? bHover : bElevated));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(g_rects.chipBit[i], 7.0f, 7.0f), sel ? bAccent : (hov ? bBorderStr : bBorder), 1.0f);
        d2d_draw(rt, bitLabels[i], g_tf_mono_center, g_rects.chipBit[i], sel ? bAccent : bTextSec);
        if (g_focus == (HoverTarget)((int)HoverTarget::Bit0 + i)) draw_focus_outline(rt, g_rects.chipBit[i], 7.0f, bAccent);
    }

    d2d_draw(rt, L"Custom Bitrate (2 to 60 Mbps)", g_tf_sub, D2D1::RectF(36.0f, 206.0f, 300.0f, 224.0f), bMuted);
    std::wstring mbpsVal = std::to_wstring(cfg.bitrate_mbps) + L" Mbps";
    d2d_draw(rt, mbpsVal, g_tf_mono_right, D2D1::RectF(310.0f, 206.0f, 444.0f, 224.0f), bAccent);

    g_rects.bitrateTrack = D2D1::RectF(36.0f, 226.0f, 444.0f, 258.0f);
    float sLeft = 44.0f;
    float sRight = 436.0f;
    float norm = (float)(cfg.bitrate_mbps - 2) / 58.0f;
    float thumbX = sLeft + norm * (sRight - sLeft);
    float trackY = 242.0f;

    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sLeft, trackY - 3.0f, sRight, trackY + 3.0f), 3.0f, 3.0f), bElevated);
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(sLeft, trackY - 3.0f, thumbX, trackY + 3.0f), 3.0f, 3.0f), bAccent);

    bool sHov = (g_hover == HoverTarget::BitrateSlider || g_dragging_bitrate);
    D2D1_ELLIPSE thumbOuter = D2D1::Ellipse(D2D1::Point2F(thumbX, trackY), sHov ? 8.0f : 7.0f, sHov ? 8.0f : 7.0f);
    rt->FillEllipse(thumbOuter, bText);
    rt->DrawEllipse(thumbOuter, bCanvas, 2.0f);
    if (g_focus == HoverTarget::BitrateSlider) draw_focus_outline(rt, g_rects.bitrateTrack, 6.0f, bAccent);

    // --- CARD 2: AUDIO ROUTING (x: 20..460, y: 288..400) ---
    D2D1_RECT_F c2 = D2D1::RectF(20.0f, 288.0f, 460.0f, 400.0f);
    rt->FillRoundedRectangle(D2D1::RoundedRect(c2, 12.0f, 12.0f), bSurface);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(c2, 12.0f, 12.0f), bBorder, 1.0f);

    g_rects.rowAudio = D2D1::RectF(20.0f, 288.0f, 460.0f, 344.0f);
    if (g_hover == HoverTarget::Audio) {
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(24.0f, 292.0f, 456.0f, 341.0f), 8.0f, 8.0f), bElevated);
    }
    d2d_draw(rt, L"System Audio", g_tf_body, D2D1::RectF(36.0f, 297.0f, 340.0f, 316.0f), bText);
    d2d_draw(rt, cfg.audio_enabled ? L"Capturing desktop loopback audio" : L"Video only (audio muted)", g_tf_sub, D2D1::RectF(36.0f, 317.0f, 360.0f, 334.0f), bMuted);
    draw_toggle_switch(rt, 444.0f, 316.0f, cfg.audio_enabled, false, bAccent, bElevated, bBorderStr, bText);
    if (g_focus == HoverTarget::Audio) draw_focus_outline(rt, g_rects.rowAudio, 10.0f, bAccent);

    rt->DrawLine(D2D1::Point2F(36.0f, 344.0f), D2D1::Point2F(444.0f, 344.0f), bBorder, 1.0f);

    g_rects.rowDiscord = D2D1::RectF(20.0f, 344.0f, 460.0f, 400.0f);
    if (g_hover == HoverTarget::Discord) {
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(24.0f, 347.0f, 456.0f, 396.0f), 8.0f, 8.0f), bElevated);
    }
    DWORD discordPid = cfg.exclude_discord ? find_discord_root_pid() : 0;
    d2d_draw(rt, L"Exclude Discord Audio", g_tf_body, D2D1::RectF(36.0f, 353.0f, 340.0f, 372.0f), cfg.audio_enabled ? bText : bMuted);
    std::wstring discSub;
    ID2D1Brush* discSubBrush = bMuted;
    if (!cfg.audio_enabled) {
        discSub = L"Enable system audio to configure";
    } else if (cfg.exclude_discord) {
        if (discordPid) {
            discSub = L"Isolated cleanly (Discord PID " + std::to_wstring(discordPid) + L")";
            discSubBrush = bGreen;
        } else {
            discSub = L"Armed (waiting for Discord voice)";
            discSubBrush = bAccent;
        }
    } else {
        discSub = L"Discord voice included in stream";
    }
    d2d_draw(rt, discSub, g_tf_sub, D2D1::RectF(36.0f, 373.0f, 380.0f, 390.0f), discSubBrush);
    draw_toggle_switch(rt, 444.0f, 372.0f, cfg.exclude_discord, !cfg.audio_enabled, bAccent, bElevated, bBorderStr, bText);
    if (g_focus == HoverTarget::Discord) draw_focus_outline(rt, g_rects.rowDiscord, 10.0f, bAccent);

    // --- CARD 3: SHARE LINK & SERVER (x: 20..460, y: 412..510) ---
    D2D1_RECT_F c3 = D2D1::RectF(20.0f, 412.0f, 460.0f, 510.0f);
    rt->FillRoundedRectangle(D2D1::RoundedRect(c3, 12.0f, 12.0f), bSurface);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(c3, 12.0f, 12.0f), bBorder, 1.0f);

    d2d_draw(rt, L"Viewer Link", g_tf_section, D2D1::RectF(36.0f, 424.0f, 160.0f, 442.0f), bText);

    g_rects.boxServer = D2D1::RectF(196.0f, 420.0f, 444.0f, 444.0f);
    bool srvHov = (g_hover == HoverTarget::Server);
    if (srvHov) {
        rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.boxServer, 6.0f, 6.0f), bElevated);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(g_rects.boxServer, 6.0f, 6.0f), bBorderStr, 1.0f);
    }
    std::string hostShort = cfg.server_url;
    if (hostShort.rfind("https://", 0) == 0) hostShort = hostShort.substr(8);
    else if (hostShort.rfind("http://", 0) == 0) hostShort = hostShort.substr(7);
    while (!hostShort.empty() && hostShort.back() == '/') hostShort.pop_back();
    if (hostShort.size() > 26) hostShort = hostShort.substr(0, 24) + "..";
    std::wstring srvText = to_wide(hostShort) + L"  (Edit)";
    d2d_draw(rt, srvText, g_tf_mono_right, D2D1::RectF(196.0f, 424.0f, 438.0f, 442.0f), srvHov ? bAccent : bMuted);
    if (g_focus == HoverTarget::Server) draw_focus_outline(rt, g_rects.boxServer, 6.0f, bAccent);

    D2D1_RECT_F urlWell = D2D1::RectF(36.0f, 454.0f, 286.0f, 492.0f);
    rt->FillRoundedRectangle(D2D1::RoundedRect(urlWell, 8.0f, 8.0f), bCanvas);
    rt->DrawRoundedRectangle(D2D1::RoundedRect(urlWell, 8.0f, 8.0f), bBorder, 1.0f);
    std::string viewUrl = cfg.server_url;
    while (!viewUrl.empty() && viewUrl.back() == '/') viewUrl.pop_back();
    viewUrl += "/r/" + cfg.room_id;
    std::string dispUrl = viewUrl;
    if (dispUrl.rfind("https://", 0) == 0) dispUrl = dispUrl.substr(8);
    else if (dispUrl.rfind("http://", 0) == 0) dispUrl = dispUrl.substr(7);
    if (dispUrl.size() > 33) dispUrl = ".." + dispUrl.substr(dispUrl.size() - 31);
    d2d_draw(rt, to_wide(dispUrl), g_tf_mono_sm, D2D1::RectF(46.0f, 466.0f, 280.0f, 484.0f), bTextSec);

    g_rects.btnCopy    = D2D1::RectF(292.0f, 454.0f, 348.0f, 492.0f);
    g_rects.btnNewRoom = D2D1::RectF(354.0f, 454.0f, 396.0f, 492.0f);
    g_rects.btnOpenWeb = D2D1::RectF(402.0f, 454.0f, 444.0f, 492.0f);

    auto draw_small_btn = [&](const D2D1_RECT_F& r, const wchar_t* lbl, HoverTarget t, bool copied = false) {
        bool h = (g_hover == t);
        rt->FillRoundedRectangle(D2D1::RoundedRect(r, 8.0f, 8.0f), copied ? bGreenBg : (h ? bHover : bElevated));
        rt->DrawRoundedRectangle(D2D1::RoundedRect(r, 8.0f, 8.0f), copied ? bGreen : (h ? bBorderStr : bBorder), 1.0f);
        d2d_draw(rt, lbl, g_tf_body_center, r, copied ? bGreen : (h ? bText : bTextSec));
        if (g_focus == t) draw_focus_outline(rt, r, 8.0f, bAccent);
    };
    draw_small_btn(g_rects.btnCopy, g_copied_feedback ? L"Copied" : L"Copy", HoverTarget::Copy, g_copied_feedback);
    draw_small_btn(g_rects.btnNewRoom, L"New", HoverTarget::NewRoom);
    draw_small_btn(g_rects.btnOpenWeb, L"Open", HoverTarget::OpenWeb);

    // --- PRIMARY ACTION BUTTON (x: 20..460, y: 522..566) ---
    g_rects.btnStartStop = D2D1::RectF(20.0f, 522.0f, 460.0f, 566.0f);
    bool actHov = (g_hover == HoverTarget::StartStop);
    if (st.is_streaming) {
        rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.btnStartStop, 10.0f, 10.0f), actHov ? bRed : bRedBg);
        rt->DrawRoundedRectangle(D2D1::RoundedRect(g_rects.btnStartStop, 10.0f, 10.0f), bRed, 1.0f);
        d2d_draw(rt, L"Stop Sharing", g_tf_btn, g_rects.btnStartStop, actHov ? bText : bRedText);
    } else {
        rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.btnStartStop, 10.0f, 10.0f), actHov ? bAccentHover : bAccent);
        d2d_draw(rt, L"Start Sharing", g_tf_btn, g_rects.btnStartStop, bTextDark);
    }
    if (g_focus == HoverTarget::StartStop) draw_focus_outline(rt, g_rects.btnStartStop, 10.0f, bText);

    if (!st.error_msg.empty()) {
        d2d_draw(rt, to_wide(st.error_msg), g_tf_hint_center, D2D1::RectF(20.0f, 572.0f, 460.0f, 592.0f), bRedText);
    } else {
        d2d_draw(rt, L"Space to toggle  ·  Ctrl+C copy link  ·  1-4 resolution", g_tf_hint_center, D2D1::RectF(20.0f, 572.0f, 460.0f, 592.0f), bMuted);
    }

    rt->EndDraw();

    bSurface->Release();
    bElevated->Release();
    bHover->Release();
    bActive->Release();
    bCanvas->Release();
    bText->Release();
    bTextSec->Release();
    bMuted->Release();
    bTextDark->Release();
    bBorder->Release();
    bBorderStr->Release();
    bHighlight->Release();
    bAccent->Release();
    bAccentHover->Release();
    bAccentBg->Release();
    bGreen->Release();
    bGreenBg->Release();
    bRed->Release();
    bRedBg->Release();
    bRedText->Release();
}

static void render() {
    render_to(g_rt);
}

static HoverTarget compute_hover(float x, float y) {
    if (hit_test(g_rects.btnClose, x, y)) return HoverTarget::Close;
    if (hit_test(g_rects.btnMinimize, x, y)) return HoverTarget::Minimize;
    if (hit_test(g_rects.boxServer, x, y)) return HoverTarget::Server;
    for (int i = 0; i < 4; ++i) {
        if (hit_test(g_rects.chipRes[i], x, y)) return (HoverTarget)((int)HoverTarget::Res0 + i);
    }
    if (hit_test(g_rects.chipFps[0], x, y)) return HoverTarget::Fps0;
    if (hit_test(g_rects.chipFps[1], x, y)) return HoverTarget::Fps1;
    for (int i = 0; i < 5; ++i) {
        if (hit_test(g_rects.chipBit[i], x, y)) return (HoverTarget)((int)HoverTarget::Bit0 + i);
    }
    if (hit_test(g_rects.bitrateTrack, x, y)) return HoverTarget::BitrateSlider;
    if (hit_test(g_rects.rowAudio, x, y)) return HoverTarget::Audio;
    if (hit_test(g_rects.rowDiscord, x, y)) return HoverTarget::Discord;
    if (hit_test(g_rects.btnCopy, x, y)) return HoverTarget::Copy;
    if (hit_test(g_rects.btnNewRoom, x, y)) return HoverTarget::NewRoom;
    if (hit_test(g_rects.btnOpenWeb, x, y)) return HoverTarget::OpenWeb;
    if (hit_test(g_rects.btnStartStop, x, y)) return HoverTarget::StartStop;
    return HoverTarget::None;
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

static void edit_server_dialog(HWND hwnd) {
    std::string clip_text;
    if (OpenClipboard(hwnd)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* pText = (wchar_t*)GlobalLock(hData);
            if (pText) {
                clip_text = to_utf8(std::wstring(pText));
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }
    while (!clip_text.empty() && (clip_text.back() == '\r' || clip_text.back() == '\n' || clip_text.back() == ' ')) clip_text.pop_back();
    while (!clip_text.empty() && clip_text.front() == ' ') clip_text.erase(clip_text.begin());

    std::wstring cfg_path = get_app_dir() + L"\\server.txt";
    if (clip_text.rfind("http://", 0) == 0 || clip_text.rfind("https://", 0) == 0) {
        std::wstring msg = L"Set Server URL to:\n\n" + to_wide(clip_text) + L"\n\n(Click 'No' to open server.txt in Notepad)";
        int res = MessageBoxW(hwnd, msg.c_str(), L"Ghost Share Jor - Server URL", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (res == IDYES) {
            g_cfg.server_url = clip_text;
            save_config();
            if (g_state.is_streaming) start_stream_process();
        } else if (res == IDNO) {
            ShellExecuteW(nullptr, L"open", L"notepad.exe", cfg_path.c_str(), nullptr, SW_SHOW);
        }
    } else {
        int res = MessageBoxW(hwnd, L"Click 'OK' to open server.txt in Notepad.\n\nTip: Copy an http/https URL first and click Edit to apply it immediately.", L"Ghost Share Jor - Server URL", MB_OKCANCEL | MB_ICONINFORMATION);
        if (res == IDOK) {
            ShellExecuteW(nullptr, L"open", L"notepad.exe", cfg_path.c_str(), nullptr, SW_SHOW);
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void activate_target(HWND hwnd, HoverTarget target) {
    switch (target) {
    case HoverTarget::Server:
        edit_server_dialog(hwnd);
        break;
    case HoverTarget::Res0:
    case HoverTarget::Res1:
    case HoverTarget::Res2:
    case HoverTarget::Res3:
        g_cfg.resolution = (ScreenResolution)((int)target - (int)HoverTarget::Res0);
        if (g_state.is_streaming) start_stream_process();
        break;
    case HoverTarget::Fps0:
        g_cfg.fps = ScreenFps::Fps60;
        if (g_state.is_streaming) start_stream_process();
        break;
    case HoverTarget::Fps1:
        g_cfg.fps = ScreenFps::Fps30;
        if (g_state.is_streaming) start_stream_process();
        break;
    case HoverTarget::Bit0:
    case HoverTarget::Bit1:
    case HoverTarget::Bit2:
    case HoverTarget::Bit3:
    case HoverTarget::Bit4:
        g_cfg.bitrate_mbps = BITRATE_PRESETS[(int)target - (int)HoverTarget::Bit0];
        if (g_state.is_streaming) start_stream_process();
        break;
    case HoverTarget::Audio:
        g_cfg.audio_enabled = !g_cfg.audio_enabled;
        if (g_state.is_streaming) start_stream_process();
        break;
    case HoverTarget::Discord:
        if (g_cfg.audio_enabled) {
            g_cfg.exclude_discord = !g_cfg.exclude_discord;
            if (g_state.is_streaming) start_stream_process();
        }
        break;
    case HoverTarget::Copy:
        copy_room_link(hwnd);
        break;
    case HoverTarget::NewRoom:
        g_cfg.room_id = generate_random_room();
        if (g_state.is_streaming) start_stream_process();
        break;
    case HoverTarget::OpenWeb: {
        std::string view_url = g_cfg.server_url;
        while (!view_url.empty() && view_url.back() == '/') view_url.pop_back();
        view_url += "/r/" + g_cfg.room_id;
        ShellExecuteW(nullptr, L"open", to_wide(view_url).c_str(), nullptr, nullptr, SW_SHOW);
        break;
    }
    case HoverTarget::StartStop:
        if (g_state.is_streaming) stop_stream_process();
        else start_stream_process();
        break;
    default:
        break;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

static const HoverTarget FOCUS_ORDER[] = {
    HoverTarget::Res0, HoverTarget::Res1, HoverTarget::Res2, HoverTarget::Res3,
    HoverTarget::Fps0, HoverTarget::Fps1,
    HoverTarget::Bit0, HoverTarget::Bit1, HoverTarget::Bit2, HoverTarget::Bit3, HoverTarget::Bit4,
    HoverTarget::BitrateSlider,
    HoverTarget::Audio, HoverTarget::Discord,
    HoverTarget::Server, HoverTarget::Copy, HoverTarget::NewRoom, HoverTarget::OpenWeb,
    HoverTarget::StartStop
};

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (pt.y < 56 && !hit_test(g_rects.btnClose, (float)pt.x, (float)pt.y)
            && !hit_test(g_rects.btnMinimize, (float)pt.x, (float)pt.y)) {
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, (g_hover != HoverTarget::None || g_dragging_bitrate) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        float x = (float)GET_X_LPARAM(lParam);
        float y = (float)GET_Y_LPARAM(lParam);
        if (g_dragging_bitrate) {
            int nextBit = bitrate_from_x(x);
            if (nextBit != g_cfg.bitrate_mbps) {
                g_cfg.bitrate_mbps = nextBit;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        if (!g_tracking_mouse) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_tracking_mouse = true;
        }
        HoverTarget next = compute_hover(x, y);
        if (next != g_hover) {
            g_hover = next;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE: {
        g_tracking_mouse = false;
        if (g_hover != HoverTarget::None) {
            g_hover = HoverTarget::None;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        float x = (float)GET_X_LPARAM(lParam);
        float y = (float)GET_Y_LPARAM(lParam);
        g_focus = HoverTarget::None;

        if (hit_test(g_rects.btnClose, x, y)) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (hit_test(g_rects.btnMinimize, x, y)) {
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        }
        if (hit_test(g_rects.bitrateTrack, x, y)) {
            g_dragging_bitrate = true;
            g_drag_start_bitrate = g_cfg.bitrate_mbps;
            g_cfg.bitrate_mbps = bitrate_from_x(x);
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        HoverTarget t = compute_hover(x, y);
        if (t != HoverTarget::None) {
            activate_target(hwnd, t);
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP: {
        if (g_dragging_bitrate) {
            g_dragging_bitrate = false;
            ReleaseCapture();
            if (g_state.is_streaming && g_cfg.bitrate_mbps != g_drag_start_bitrate) {
                start_stream_process();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int step = (delta > 0) ? 2 : -2;
        int nextBit = std::clamp(g_cfg.bitrate_mbps + step, 2, 60);
        if (nextBit != g_cfg.bitrate_mbps) {
            g_cfg.bitrate_mbps = nextBit;
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl && wParam == 'C') {
            copy_room_link(hwnd);
            return 0;
        }
        if (wParam == VK_SPACE) {
            activate_target(hwnd, HoverTarget::StartStop);
            return 0;
        }
        if (wParam == VK_RETURN && g_focus != HoverTarget::None) {
            activate_target(hwnd, g_focus);
            return 0;
        }
        if (wParam == VK_TAB) {
            int n = (int)(sizeof(FOCUS_ORDER) / sizeof(FOCUS_ORDER[0]));
            int idx = -1;
            for (int i = 0; i < n; ++i) {
                if (FOCUS_ORDER[i] == g_focus) { idx = i; break; }
            }
            if (idx < 0) idx = shift ? n - 1 : 0;
            else idx = (idx + (shift ? -1 : 1) + n) % n;
            g_focus = FOCUS_ORDER[idx];
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_LEFT || wParam == VK_RIGHT) {
            int step = (wParam == VK_RIGHT) ? 2 : -2;
            g_cfg.bitrate_mbps = std::clamp(g_cfg.bitrate_mbps + step, 2, 60);
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam >= '1' && wParam <= '4') {
            g_cfg.resolution = (ScreenResolution)(wParam - '1');
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_TIMER: {
        if (g_copied_feedback && (GetTickCount() - g_copied_tick > 2500)) {
            g_copied_feedback = false;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        render();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT: {
        HDC hdc = (HDC)wParam;
        if (hdc && g_d2d) {
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT
            );
            ID2D1DCRenderTarget* dcRt = nullptr;
            if (SUCCEEDED(g_d2d->CreateDCRenderTarget(&props, &dcRt)) && dcRt) {
                RECT rc = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
                if (SUCCEEDED(dcRt->BindDC(hdc, &rc))) {
                    render_to(dcRt);
                }
                dcRt->Release();
            }
        }
        return 0;
    }
    case WM_DESTROY: {
        g_quitting.store(true);
        stop_stream_process();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int) {
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

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw);
    g_fonts = StudioTheme::load_fonts(g_dw);

    const wchar_t* bodyF = g_fonts.bodyFamily.c_str();
    const wchar_t* monoF = g_fonts.monoFamily.c_str();

    g_dw->CreateTextFormat(bodyF, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.5f, L"en-US", &g_tf_title);
    g_dw->CreateTextFormat(bodyF, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-US", &g_tf_section);
    g_dw->CreateTextFormat(bodyF, nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-US", &g_tf_body);
    g_dw->CreateTextFormat(bodyF, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-US", &g_tf_body_center);
    g_tf_body_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_body_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(bodyF, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &g_tf_sub);
    g_dw->CreateTextFormat(monoF, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-US", &g_tf_mono);
    g_dw->CreateTextFormat(monoF, nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &g_tf_mono_right);
    g_tf_mono_right->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    g_dw->CreateTextFormat(monoF, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-US", &g_tf_mono_sm);
    g_dw->CreateTextFormat(monoF, nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &g_tf_mono_center);
    g_tf_mono_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_mono_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(bodyF, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-US", &g_tf_hint_center);
    g_tf_hint_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(bodyF, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-US", &g_tf_btn);
    g_tf_btn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_btn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"GhostShareJorWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    RegisterClassExW(&wc);

    int scr_w = GetSystemMetrics(SM_CXSCREEN);
    int scr_h = GetSystemMetrics(SM_CYSCREEN);
    int wx = (scr_w - WINDOW_WIDTH) / 2;
    int wy = (scr_h - WINDOW_HEIGHT) / 2;

    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"Ghost Share Jor",
        WS_POPUP | WS_VISIBLE,
        wx, wy, WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr, nullptr, hInstance, nullptr
    );

    // Enable native Windows 11 rounded corners + dark border when supported
    DWORD cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
    COLORREF borderCol = RGB(0x27, 0x27, 0x2B);
    DwmSetWindowAttribute(g_hwnd, DWMWA_BORDER_COLOR, &borderCol, sizeof(borderCol));

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp = D2D1::HwndRenderTargetProperties(
        g_hwnd, D2D1::SizeU(WINDOW_WIDTH, WINDOW_HEIGHT)
    );
    g_d2d->CreateHwndRenderTarget(rtp, hrtp, &g_rt);

    SetTimer(g_hwnd, 1, 500, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_tf_title) g_tf_title->Release();
    if (g_tf_section) g_tf_section->Release();
    if (g_tf_body) g_tf_body->Release();
    if (g_tf_body_center) g_tf_body_center->Release();
    if (g_tf_sub) g_tf_sub->Release();
    if (g_tf_mono) g_tf_mono->Release();
    if (g_tf_mono_right) g_tf_mono_right->Release();
    if (g_tf_mono_sm) g_tf_mono_sm->Release();
    if (g_tf_mono_center) g_tf_mono_center->Release();
    if (g_tf_hint_center) g_tf_hint_center->Release();
    if (g_tf_btn) g_tf_btn->Release();
    if (g_rt) g_rt->Release();
    if (g_d2d) g_d2d->Release();
    if (g_dw) g_dw->Release();
    if (g_job) CloseHandle(g_job);

    return 0;
}
