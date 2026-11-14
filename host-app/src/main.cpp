#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include "ghost_share_core.h"

static const int WINDOW_WIDTH = 560;
static const int WINDOW_HEIGHT = 656;

// State
static HWND g_hwnd = nullptr;
static ID2D1Factory* g_d2d = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static IDWriteFactory* g_dw = nullptr;
static IDWriteTextFormat* g_tf_title = nullptr;
static IDWriteTextFormat* g_tf_title_center = nullptr;
static IDWriteTextFormat* g_tf_bold = nullptr;
static IDWriteTextFormat* g_tf_regular = nullptr;
static IDWriteTextFormat* g_tf_small = nullptr;
static IDWriteTextFormat* g_tf_mono = nullptr;
static IDWriteTextFormat* g_tf_btn_center = nullptr;
static IDWriteTextFormat* g_tf_ctrl_center = nullptr;
static IDWriteTextFormat* g_tf_section_lbl = nullptr;
static IDWriteTextFormat* g_tf_badge_center = nullptr;

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

static const int POPULAR_BITRATES[5] = { 8, 12, 16, 25, 40 };

enum class HoverTarget {
    None = 0,
    Close,
    Minimize,
    Server,
    Res0, Res1, Res2, Res3,
    Fps0, Fps1,
    Bit0, Bit1, Bit2, Bit3, Bit4,
    BitMinus,
    BitPlus,
    Audio,
    Discord,
    Copy,
    NewRoom,
    OpenWeb,
    StartStop
};

static HoverTarget g_hover = HoverTarget::None;
static bool g_tracking_mouse = false;

// Interaction rects
struct UiRects {
    D2D1_RECT_F btnClose;
    D2D1_RECT_F btnMinimize;
    D2D1_RECT_F boxServer;

    // Resolution chips (0: Source, 1: 1440p, 2: 1080p, 3: 720p)
    D2D1_RECT_F chipRes[4];

    // Framerate chips (0: 60 FPS, 1: 30 FPS)
    D2D1_RECT_F chipFps[2];

    // Bitrate chips (8M, 12M, 16M, 25M, 40M) & steppers
    D2D1_RECT_F chipBitrate[5];
    D2D1_RECT_F btnBitrateMinus;
    D2D1_RECT_F btnBitratePlus;

    // Audio & Discord Isolation cards
    D2D1_RECT_F btnAudio;
    D2D1_RECT_F btnDiscord;

    // Link card buttons
    D2D1_RECT_F btnCopy;
    D2D1_RECT_F btnNewRoom;
    D2D1_RECT_F btnOpenWeb;

    // Big Action Button
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
        g_state.status_text = "Stream stopped";
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
        g_state.status_text = "Connecting to server...";
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
            g_state.status_text = "Screen share could not start";
            g_state.error_msg = "Streaming runtime is missing from the app folder.";
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
            g_state.status_text = "Screen share could not start";
            g_state.error_msg = "Could not start the bundled screen capture.";
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
            g_state.status_text = "LIVE — Screen sharing active";
        }
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);

        std::string tail = read_log_tail(log_path);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_stream_proc = nullptr;
            bool unexpected = g_state.status_text.find("LIVE") != std::string::npos
                || g_state.status_text.find("Connecting") != std::string::npos;
            g_state.is_streaming = false;
            if (unexpected) {
                g_state.status_text = "Stream ended";
                if (!tail.empty()) g_state.error_msg = tail;
            }
        }
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    }).detach();
}

static void draw_ghost_emblem(ID2D1RenderTarget* rt, float cx, float cy, float r, ID2D1Brush* brush, ID2D1Brush* eyeBrush) {
    D2D1_ELLIPSE head = D2D1::Ellipse(D2D1::Point2F(cx, cy - r * 0.2f), r, r * 0.85f);
    rt->FillEllipse(head, brush);
    D2D1_RECT_F body = D2D1::RectF(cx - r, cy - r * 0.2f, cx + r, cy + r * 0.8f);
    rt->FillRectangle(body, brush);

    float foot_w = (r * 2.0f) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        float fx = (cx - r) + i * foot_w + foot_w * 0.5f;
        D2D1_ELLIPSE foot = D2D1::Ellipse(D2D1::Point2F(fx, cy + r * 0.8f), foot_w * 0.5f, r * 0.35f);
        rt->FillEllipse(foot, brush);
    }
    float eye_r = r * 0.17f;
    D2D1_ELLIPSE eyeL = D2D1::Ellipse(D2D1::Point2F(cx - r * 0.35f, cy - r * 0.2f), eye_r, eye_r * 1.3f);
    D2D1_ELLIPSE eyeR = D2D1::Ellipse(D2D1::Point2F(cx + r * 0.35f, cy - r * 0.2f), eye_r, eye_r * 1.3f);
    rt->FillEllipse(eyeL, eyeBrush);
    rt->FillEllipse(eyeR, eyeBrush);
}

static void render() {
    if (!g_rt) return;
    g_rt->BeginDraw();
    g_rt->Clear(D2D1::ColorF(0x050507)); // Deep Obsidian Studio Void

    ID2D1SolidColorBrush* bCard = nullptr;
    ID2D1SolidColorBrush* bCardHover = nullptr;
    ID2D1SolidColorBrush* bWell = nullptr;
    ID2D1SolidColorBrush* bText = nullptr;
    ID2D1SolidColorBrush* bSubText = nullptr;
    ID2D1SolidColorBrush* bMuted = nullptr;
    ID2D1SolidColorBrush* bAccent = nullptr;
    ID2D1SolidColorBrush* bAccentHover = nullptr;
    ID2D1SolidColorBrush* bAccentDim = nullptr;
    ID2D1SolidColorBrush* bRed = nullptr;
    ID2D1SolidColorBrush* bRedHover = nullptr;
    ID2D1SolidColorBrush* bGreen = nullptr;
    ID2D1SolidColorBrush* bBorder = nullptr;
    ID2D1SolidColorBrush* bBorderHover = nullptr;
    ID2D1SolidColorBrush* bBg = nullptr;

    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x0e0e13), &bCard);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x171720), &bCardHover);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x09090d), &bWell);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0xfbfbfb), &bText);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0xa1a1aa), &bSubText);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x71717a), &bMuted);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x6366f1), &bAccent);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x7578ff), &bAccentHover);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x6366f1, 0.14f), &bAccentDim);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0xef4444), &bRed);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0xf87171), &bRedHover);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x10b981), &bGreen);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x20202a), &bBorder);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x3a3a4c), &bBorderHover);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x050507), &bBg);

    // Outer 1px Studio Frame Border
    g_rt->DrawRectangle(D2D1::RectF(0.5f, 0.5f, WINDOW_WIDTH - 0.5f, WINDOW_HEIGHT - 0.5f), bBorder, 1.0f);

    // Header & Emblem
    draw_ghost_emblem(g_rt, 36.0f, 32.0f, 14.0f, bAccent, bBg);

    D2D1_RECT_F rTitle = D2D1::RectF(60, 14, 340, 36);
    d2d_draw(g_rt, L"GHOST SHARE JOR", g_tf_title, rTitle, bText);

    D2D1_RECT_F rSub = D2D1::RectF(60, 36, 360, 52);
    d2d_draw(g_rt, L"STUDIO HUD \u2022 ZERO-COPY WEBRTC", g_tf_small, rSub, bMuted);

    // Top-right Live / Standby Pill
    D2D1_RECT_F rStatusPill = D2D1::RectF(WINDOW_WIDTH - 166, 16, WINDOW_WIDTH - 78, 36);
    D2D1_ROUNDED_RECT rcPill = D2D1::RoundedRect(rStatusPill, 10, 10);
    g_rt->FillRoundedRectangle(rcPill, g_state.is_streaming ? bAccentDim : bCard);
    g_rt->DrawRoundedRectangle(rcPill, g_state.is_streaming ? bGreen : bBorder, 1.0f);
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(rStatusPill.left + 14.0f, 26.0f), 3.5f, 3.5f),
                      g_state.is_streaming ? bGreen : bMuted);
    D2D1_RECT_F rPillTxt = D2D1::RectF(rStatusPill.left + 16, 16, rStatusPill.right - 4, 36);
    d2d_draw(g_rt, g_state.is_streaming ? L"LIVE" : L"STANDBY", g_tf_badge_center, rPillTxt,
             g_state.is_streaming ? bGreen : bMuted);

    // Window controls (Min / Close)
    g_rects.btnMinimize = D2D1::RectF(WINDOW_WIDTH - 66, 14, WINDOW_WIDTH - 40, 38);
    g_rects.btnClose = D2D1::RectF(WINDOW_WIDTH - 36, 14, WINDOW_WIDTH - 10, 38);
    if (g_hover == HoverTarget::Minimize) {
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.btnMinimize, 6, 6), bCardHover);
    }
    if (g_hover == HoverTarget::Close) {
        g_rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.btnClose, 6, 6), bRed);
    }
    d2d_draw(g_rt, L"\u2014", g_tf_ctrl_center, g_rects.btnMinimize, g_hover == HoverTarget::Minimize ? bText : bMuted);
    d2d_draw(g_rt, L"\u2715", g_tf_ctrl_center, g_rects.btnClose, g_hover == HoverTarget::Close ? bText : bMuted);

    // Server URL Card
    D2D1_ROUNDED_RECT rcServer = D2D1::RoundedRect(D2D1::RectF(24, 62, WINDOW_WIDTH - 24, 112), 8, 8);
    g_rects.boxServer = rcServer.rect;
    bool srvHover = (g_hover == HoverTarget::Server);
    g_rt->FillRoundedRectangle(rcServer, srvHover ? bCardHover : bCard);
    g_rt->DrawRoundedRectangle(rcServer, srvHover ? bBorderHover : bBorder, 1.0f);

    D2D1_RECT_F rSrvLbl = D2D1::RectF(36, 69, 380, 84);
    d2d_draw(g_rt, L"SERVER ENDPOINT (CLICK TO EDIT)", g_tf_section_lbl, rSrvLbl, bMuted);
    D2D1_RECT_F rSrvVal = D2D1::RectF(36, 85, WINDOW_WIDTH - 36, 106);
    std::wstring wsServer = to_wide(g_cfg.server_url);
    d2d_draw(g_rt, wsServer, g_tf_mono, rSrvVal, bText);

    // -------------------------------------------------------------
    // SECTION 1: RESOLUTION (Unified Segmented Bar)
    // -------------------------------------------------------------
    D2D1_RECT_F rResLbl = D2D1::RectF(24, 123, WINDOW_WIDTH - 24, 138);
    d2d_draw(g_rt, L"RESOLUTION", g_tf_section_lbl, rResLbl, bMuted);

    float total_usable_w = (float)(WINDOW_WIDTH - 48); // 512
    D2D1_ROUNDED_RECT rcResWell = D2D1::RoundedRect(D2D1::RectF(24, 140, WINDOW_WIDTH - 24, 178), 8, 8);
    g_rt->FillRoundedRectangle(rcResWell, bWell);
    g_rt->DrawRoundedRectangle(rcResWell, bBorder, 1.0f);

    const wchar_t* res_names[4] = { L"Source", L"1440p QHD", L"1080p FHD", L"720p HD" };
    float seg_pad = 4.0f;
    float chip_w = (total_usable_w - (seg_pad * 5.0f)) / 4.0f;
    float chip_y = 144.0f;
    float chip_h = 30.0f;

    for (int i = 0; i < 4; ++i) {
        float x1 = 24.0f + seg_pad + i * (chip_w + seg_pad);
        float x2 = x1 + chip_w;
        g_rects.chipRes[i] = D2D1::RectF(x1, chip_y, x2, chip_y + chip_h);
        D2D1_ROUNDED_RECT rcChip = D2D1::RoundedRect(g_rects.chipRes[i], 6, 6);

        bool is_active = ((int)g_cfg.resolution == i);
        bool is_hover = ((int)g_hover == (int)HoverTarget::Res0 + i);

        if (is_active) {
            g_rt->FillRoundedRectangle(rcChip, is_hover ? bAccentHover : bAccent);
            d2d_draw(g_rt, res_names[i], g_tf_btn_center, g_rects.chipRes[i], bText);
        } else {
            if (is_hover) g_rt->FillRoundedRectangle(rcChip, bCardHover);
            d2d_draw(g_rt, res_names[i], g_tf_btn_center, g_rects.chipRes[i], is_hover ? bText : bSubText);
        }
    }

    // -------------------------------------------------------------
    // SECTION 2: FRAMERATE & BITRATE CONTROLS
    // -------------------------------------------------------------
    D2D1_RECT_F rFpsBitLbl = D2D1::RectF(24, 190, 320, 205);
    d2d_draw(g_rt, L"FRAMERATE & BITRATE", g_tf_section_lbl, rFpsBitLbl, bMuted);

    wchar_t rate_badge[64];
    swprintf_s(rate_badge, L"%s \u2022 %d Mbps", fps_title(g_cfg.fps), g_cfg.bitrate_mbps);
    D2D1_RECT_F rRateVal = D2D1::RectF(320, 189, WINDOW_WIDTH - 24, 205);
    g_tf_section_lbl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    d2d_draw(g_rt, rate_badge, g_tf_section_lbl, rRateVal, bAccent);
    g_tf_section_lbl->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);

    float row2_y = 207.0f;
    float row2_h = 34.0f;

    // FPS Segmented Well (60 FPS | 30 FPS)
    D2D1_ROUNDED_RECT rcFpsWell = D2D1::RoundedRect(D2D1::RectF(24, row2_y, 178, row2_y + row2_h), 8, 8);
    g_rt->FillRoundedRectangle(rcFpsWell, bWell);
    g_rt->DrawRoundedRectangle(rcFpsWell, bBorder, 1.0f);

    const wchar_t* fps_names[2] = { L"60 FPS", L"30 FPS" };
    float fps_w = 72.0f;
    for (int i = 0; i < 2; ++i) {
        float x1 = 27.0f + i * (fps_w + 4.0f);
        float x2 = x1 + fps_w;
        g_rects.chipFps[i] = D2D1::RectF(x1, row2_y + 3.0f, x2, row2_y + row2_h - 3.0f);
        D2D1_ROUNDED_RECT rcFps = D2D1::RoundedRect(g_rects.chipFps[i], 6, 6);

        bool is_active = (i == 0 && g_cfg.fps == ScreenFps::Fps60) || (i == 1 && g_cfg.fps == ScreenFps::Fps30);
        bool is_hover = ((int)g_hover == (int)HoverTarget::Fps0 + i);
        if (is_active) {
            g_rt->FillRoundedRectangle(rcFps, is_hover ? bAccentHover : bAccent);
            d2d_draw(g_rt, fps_names[i], g_tf_btn_center, g_rects.chipFps[i], bText);
        } else {
            if (is_hover) g_rt->FillRoundedRectangle(rcFps, bCardHover);
            d2d_draw(g_rt, fps_names[i], g_tf_btn_center, g_rects.chipFps[i], is_hover ? bText : bSubText);
        }
    }

    // Bitrate Popular Chips (8M, 12M, 16M, 25M, 40M)
    float bit_start_x = 188.0f;
    float bit_w = 46.0f;
    for (int i = 0; i < 5; ++i) {
        float x1 = bit_start_x + i * (bit_w + 5.0f);
        float x2 = x1 + bit_w;
        g_rects.chipBitrate[i] = D2D1::RectF(x1, row2_y, x2, row2_y + row2_h);
        D2D1_ROUNDED_RECT rcBit = D2D1::RoundedRect(g_rects.chipBitrate[i], 7, 7);

        bool is_active = (g_cfg.bitrate_mbps == POPULAR_BITRATES[i]);
        bool is_hover = ((int)g_hover == (int)HoverTarget::Bit0 + i);
        wchar_t b_label[16];
        swprintf_s(b_label, L"%dM", POPULAR_BITRATES[i]);

        if (is_active) {
            g_rt->FillRoundedRectangle(rcBit, is_hover ? bAccentHover : bAccent);
            d2d_draw(g_rt, b_label, g_tf_btn_center, g_rects.chipBitrate[i], bText);
        } else {
            g_rt->FillRoundedRectangle(rcBit, is_hover ? bCardHover : bCard);
            g_rt->DrawRoundedRectangle(rcBit, is_hover ? bBorderHover : bBorder, 1.0f);
            d2d_draw(g_rt, b_label, g_tf_btn_center, g_rects.chipBitrate[i], is_hover ? bText : bSubText);
        }
    }

    // Steppers: [-] and [+]
    g_rects.btnBitrateMinus = D2D1::RectF(448, row2_y, 489, row2_y + row2_h);
    g_rects.btnBitratePlus = D2D1::RectF(495, row2_y, 536, row2_y + row2_h);

    bool minusHover = (g_hover == HoverTarget::BitMinus);
    bool plusHover = (g_hover == HoverTarget::BitPlus);

    g_rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.btnBitrateMinus, 7, 7), minusHover ? bCardHover : bCard);
    g_rt->DrawRoundedRectangle(D2D1::RoundedRect(g_rects.btnBitrateMinus, 7, 7), minusHover ? bBorderHover : bBorder, 1.0f);
    d2d_draw(g_rt, L"\u2212", g_tf_ctrl_center, g_rects.btnBitrateMinus, bText);

    g_rt->FillRoundedRectangle(D2D1::RoundedRect(g_rects.btnBitratePlus, 7, 7), plusHover ? bCardHover : bCard);
    g_rt->DrawRoundedRectangle(D2D1::RoundedRect(g_rects.btnBitratePlus, 7, 7), plusHover ? bBorderHover : bBorder, 1.0f);
    d2d_draw(g_rt, L"+", g_tf_ctrl_center, g_rects.btnBitratePlus, bText);

    // Current spec preview description
    D2D1_RECT_F rSpecPrev = D2D1::RectF(24, 245, WINDOW_WIDTH - 24, 261);
    wchar_t spec_desc[128];
    swprintf_s(spec_desc, L"Profile: %s \u2022 Hardware NV12 GPU Pipeline (%d Mbps cap)",
               resolution_title(g_cfg.resolution), g_cfg.bitrate_mbps);
    d2d_draw(g_rt, spec_desc, g_tf_small, rSpecPrev, bMuted);

    // -------------------------------------------------------------
    // SECTION 3: AUDIO & DISCORD ISOLATION (Two Equal Cards)
    // -------------------------------------------------------------
    D2D1_RECT_F rAudioSectionLbl = D2D1::RectF(24, 272, WINDOW_WIDTH - 24, 287);
    d2d_draw(g_rt, L"AUDIO ROUTING & DISCORD ISOLATION", g_tf_section_lbl, rAudioSectionLbl, bMuted);

    float card_audio_w = (total_usable_w - 10.0f) / 2.0f; // 251 px
    float card_audio_y = 289.0f;
    float card_audio_h = 56.0f;

    // 1. System Audio Card
    g_rects.btnAudio = D2D1::RectF(24, card_audio_y, 24 + card_audio_w, card_audio_y + card_audio_h);
    D2D1_ROUNDED_RECT rcAudio = D2D1::RoundedRect(g_rects.btnAudio, 8, 8);
    bool audHover = (g_hover == HoverTarget::Audio);
    g_rt->FillRoundedRectangle(rcAudio, audHover ? bCardHover : bCard);
    g_rt->DrawRoundedRectangle(rcAudio, g_cfg.audio_enabled ? bAccent : (audHover ? bBorderHover : bBorder), 1.0f);

    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(39.0f, card_audio_y + 18.0f), 4.0f, 4.0f),
                      g_cfg.audio_enabled ? bGreen : bMuted);
    D2D1_RECT_F rAudTitle = D2D1::RectF(50, card_audio_y + 9, 24 + card_audio_w - 10, card_audio_y + 27);
    d2d_draw(g_rt, g_cfg.audio_enabled ? L"SYSTEM AUDIO: ON" : L"SYSTEM AUDIO: MUTED",
             g_tf_bold, rAudTitle, g_cfg.audio_enabled ? bText : bMuted);

    D2D1_RECT_F rAudDesc = D2D1::RectF(35, card_audio_y + 30, 24 + card_audio_w - 10, card_audio_y + 48);
    d2d_draw(g_rt, L"Captures game & desktop sound", g_tf_small, rAudDesc, bMuted);

    // 2. Discord Isolation Card
    DWORD discord_pid = find_discord_root_pid();
    float disc_x = 24 + card_audio_w + 10.0f;
    g_rects.btnDiscord = D2D1::RectF(disc_x, card_audio_y, WINDOW_WIDTH - 24, card_audio_y + card_audio_h);
    D2D1_ROUNDED_RECT rcDiscord = D2D1::RoundedRect(g_rects.btnDiscord, 8, 8);
    bool discHover = (g_hover == HoverTarget::Discord);
    g_rt->FillRoundedRectangle(rcDiscord, discHover ? bCardHover : bCard);
    g_rt->DrawRoundedRectangle(rcDiscord, g_cfg.exclude_discord ? bAccent : (discHover ? bBorderHover : bBorder), 1.0f);

    ID2D1SolidColorBrush* discDotBrush = bMuted;
    if (g_cfg.exclude_discord) {
        discDotBrush = (discord_pid != 0) ? bGreen : bAccent;
    }
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(disc_x + 15.0f, card_audio_y + 18.0f), 4.0f, 4.0f), discDotBrush);

    D2D1_RECT_F rDiscTitle = D2D1::RectF(disc_x + 26.0f, card_audio_y + 9, WINDOW_WIDTH - 30, card_audio_y + 27);
    d2d_draw(g_rt, g_cfg.exclude_discord ? L"EXCLUDE DISCORD: ON" : L"EXCLUDE DISCORD: OFF",
             g_tf_bold, rDiscTitle, g_cfg.exclude_discord ? bText : bMuted);

    D2D1_RECT_F rDiscDesc = D2D1::RectF(disc_x + 11.0f, card_audio_y + 30, WINDOW_WIDTH - 30, card_audio_y + 48);
    if (g_cfg.exclude_discord) {
        if (discord_pid != 0) {
            d2d_draw(g_rt, L"Discord muted in stream (no echo)", g_tf_small, rDiscDesc, bGreen);
        } else {
            d2d_draw(g_rt, L"Waiting for Discord.exe...", g_tf_small, rDiscDesc, bMuted);
        }
    } else {
        d2d_draw(g_rt, L"All PC audio broadcasted", g_tf_small, rDiscDesc, bMuted);
    }

    // -------------------------------------------------------------
    // SECTION 4: VIEWER LINK CARD
    // -------------------------------------------------------------
    D2D1_ROUNDED_RECT rcLink = D2D1::RoundedRect(D2D1::RectF(24, 357, WINDOW_WIDTH - 24, 441), 8, 8);
    g_rt->FillRoundedRectangle(rcLink, bCard);
    g_rt->DrawRoundedRectangle(rcLink, bBorder, 1.0f);

    D2D1_RECT_F rLinkLbl = D2D1::RectF(36, 365, 480, 380);
    d2d_draw(g_rt, L"VIEWER LINK (SHARE WITH FRIENDS)", g_tf_section_lbl, rLinkLbl, bMuted);

    std::string view_url = g_cfg.server_url;
    while (!view_url.empty() && view_url.back() == '/') view_url.pop_back();
    view_url += "/r/" + g_cfg.room_id;
    std::wstring wsViewUrl = to_wide(view_url);

    D2D1_RECT_F rLinkVal = D2D1::RectF(36, 382, WINDOW_WIDTH - 36, 402);
    d2d_draw(g_rt, wsViewUrl, g_tf_mono, rLinkVal, bAccent);

    // 3 Buttons: COPY LINK, NEW ROOM, OPEN WEB
    g_rects.btnCopy = D2D1::RectF(36, 406, 196, 432);
    D2D1_ROUNDED_RECT rcCopy = D2D1::RoundedRect(g_rects.btnCopy, 6, 6);
    bool copyHover = (g_hover == HoverTarget::Copy);
    g_rt->FillRoundedRectangle(rcCopy, g_copied_feedback ? bGreen : (copyHover ? bAccentHover : bAccent));
    d2d_draw(g_rt, g_copied_feedback ? L"COPIED TO CLIPBOARD" : L"COPY LINK", g_tf_btn_center, g_rects.btnCopy, bText);

    g_rects.btnNewRoom = D2D1::RectF(204, 406, 344, 432);
    D2D1_ROUNDED_RECT rcNewRoom = D2D1::RoundedRect(g_rects.btnNewRoom, 6, 6);
    bool newRoomHover = (g_hover == HoverTarget::NewRoom);
    g_rt->FillRoundedRectangle(rcNewRoom, newRoomHover ? bCardHover : bWell);
    g_rt->DrawRoundedRectangle(rcNewRoom, newRoomHover ? bBorderHover : bBorder, 1.0f);
    d2d_draw(g_rt, L"NEW ROOM", g_tf_btn_center, g_rects.btnNewRoom, bText);

    g_rects.btnOpenWeb = D2D1::RectF(352, 406, 480, 432);
    D2D1_ROUNDED_RECT rcOpenWeb = D2D1::RoundedRect(g_rects.btnOpenWeb, 6, 6);
    bool openHover = (g_hover == HoverTarget::OpenWeb);
    g_rt->FillRoundedRectangle(rcOpenWeb, openHover ? bCardHover : bWell);
    g_rt->DrawRoundedRectangle(rcOpenWeb, openHover ? bBorderHover : bBorder, 1.0f);
    d2d_draw(g_rt, L"OPEN WEB", g_tf_btn_center, g_rects.btnOpenWeb, bText);

    // -------------------------------------------------------------
    // SECTION 5: BIG ACTION BUTTON (START / STOP SHARING)
    // -------------------------------------------------------------
    g_rects.btnStartStop = D2D1::RectF(24, 453, WINDOW_WIDTH - 24, 525);
    D2D1_ROUNDED_RECT rcAction = D2D1::RoundedRect(g_rects.btnStartStop, 10, 10);
    bool actionHover = (g_hover == HoverTarget::StartStop);
    if (g_state.is_streaming) {
        g_rt->FillRoundedRectangle(rcAction, actionHover ? bRedHover : bRed);
        d2d_draw(g_rt, L"\u25A0  STOP SHARING", g_tf_title_center, g_rects.btnStartStop, bText);
    } else {
        g_rt->FillRoundedRectangle(rcAction, actionHover ? bAccentHover : bAccent);
        d2d_draw(g_rt, L"\u25CF  START SHARING", g_tf_title_center, g_rects.btnStartStop, bText);
    }

    // -------------------------------------------------------------
    // SECTION 6: STATUS BANNER & METRICS
    // -------------------------------------------------------------
    D2D1_ROUNDED_RECT rcStatus = D2D1::RoundedRect(D2D1::RectF(24, 537, WINDOW_WIDTH - 24, 626), 8, 8);
    g_rt->FillRoundedRectangle(rcStatus, bCard);
    g_rt->DrawRoundedRectangle(rcStatus, bBorder, 1.0f);

    std::wstring wsStatus = to_wide(g_state.status_text);
    D2D1_RECT_F rStatTxt = D2D1::RectF(36, 549, WINDOW_WIDTH - 36, 571);
    d2d_draw(g_rt, wsStatus, g_tf_bold, rStatTxt, g_state.is_streaming ? bGreen : bText);

    if (g_state.is_streaming) {
        uint32_t sec = (GetTickCount() - g_state.start_time) / 1000;
        int m = sec / 60;
        int s = sec % 60;
        wchar_t time_buf[128];
        swprintf_s(time_buf, L"Duration: %02d:%02d \u2022 %s @ %s \u2022 %d Mbps %s",
                   m, s, resolution_title(g_cfg.resolution), fps_title(g_cfg.fps),
                   g_cfg.bitrate_mbps,
                   g_cfg.exclude_discord ? L"\u2022 Discord Excluded" : L"");
        D2D1_RECT_F rTimeTxt = D2D1::RectF(36, 575, WINDOW_WIDTH - 36, 610);
        d2d_draw(g_rt, time_buf, g_tf_small, rTimeTxt, bSubText);
    } else if (!g_state.error_msg.empty()) {
        D2D1_RECT_F rSubTxt = D2D1::RectF(36, 573, WINDOW_WIDTH - 36, 614);
        d2d_draw(g_rt, to_wide(g_state.error_msg), g_tf_small, rSubTxt, bRed);
    } else {
        D2D1_RECT_F rSubTxt = D2D1::RectF(36, 575, WINDOW_WIDTH - 36, 610);
        d2d_draw(g_rt, L"Ready to stream via low-latency WebRTC. Click Start Sharing to go live.", g_tf_small, rSubTxt, bMuted);
    }

    // Release brushes
    if (bCard) bCard->Release();
    if (bCardHover) bCardHover->Release();
    if (bWell) bWell->Release();
    if (bText) bText->Release();
    if (bSubText) bSubText->Release();
    if (bMuted) bMuted->Release();
    if (bAccent) bAccent->Release();
    if (bAccentHover) bAccentHover->Release();
    if (bAccentDim) bAccentDim->Release();
    if (bRed) bRed->Release();
    if (bRedHover) bRedHover->Release();
    if (bGreen) bGreen->Release();
    if (bBorder) bBorder->Release();
    if (bBorderHover) bBorderHover->Release();
    if (bBg) bBg->Release();

    g_rt->EndDraw();
}

static bool hit_test(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
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
        if (hit_test(g_rects.chipBitrate[i], x, y)) return (HoverTarget)((int)HoverTarget::Bit0 + i);
    }
    if (hit_test(g_rects.btnBitrateMinus, x, y)) return HoverTarget::BitMinus;
    if (hit_test(g_rects.btnBitratePlus, x, y)) return HoverTarget::BitPlus;
    if (hit_test(g_rects.btnAudio, x, y)) return HoverTarget::Audio;
    if (hit_test(g_rects.btnDiscord, x, y)) return HoverTarget::Discord;
    if (hit_test(g_rects.btnCopy, x, y)) return HoverTarget::Copy;
    if (hit_test(g_rects.btnNewRoom, x, y)) return HoverTarget::NewRoom;
    if (hit_test(g_rects.btnOpenWeb, x, y)) return HoverTarget::OpenWeb;
    if (hit_test(g_rects.btnStartStop, x, y)) return HoverTarget::StartStop;
    return HoverTarget::None;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (pt.y < 56 && !hit_test(g_rects.btnClose, (float)pt.x, (float)pt.y) && !hit_test(g_rects.btnMinimize, (float)pt.x, (float)pt.y)) {
            return HTCAPTION; // Allow dragging the borderless window
        }
        return HTCLIENT;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, (g_hover != HoverTarget::None) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (!g_tracking_mouse) {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_tracking_mouse = true;
        }
        HoverTarget next = compute_hover((float)GET_X_LPARAM(lParam), (float)GET_Y_LPARAM(lParam));
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

        if (hit_test(g_rects.btnClose, x, y)) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (hit_test(g_rects.btnMinimize, x, y)) {
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        }

        // Action button (Start / Stop)
        if (hit_test(g_rects.btnStartStop, x, y)) {
            if (g_state.is_streaming) {
                stop_stream_process();
            } else {
                start_stream_process();
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Resolution Chips
        for (int i = 0; i < 4; ++i) {
            if (hit_test(g_rects.chipRes[i], x, y)) {
                g_cfg.resolution = (ScreenResolution)i;
                if (g_state.is_streaming) start_stream_process();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Framerate Chips
        if (hit_test(g_rects.chipFps[0], x, y)) {
            g_cfg.fps = ScreenFps::Fps60;
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (hit_test(g_rects.chipFps[1], x, y)) {
            g_cfg.fps = ScreenFps::Fps30;
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Popular Bitrate Chips
        for (int i = 0; i < 5; ++i) {
            if (hit_test(g_rects.chipBitrate[i], x, y)) {
                g_cfg.bitrate_mbps = POPULAR_BITRATES[i];
                if (g_state.is_streaming) start_stream_process();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        // Bitrate Minus / Plus
        if (hit_test(g_rects.btnBitrateMinus, x, y)) {
            g_cfg.bitrate_mbps = (std::max)(2, g_cfg.bitrate_mbps - 2);
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (hit_test(g_rects.btnBitratePlus, x, y)) {
            g_cfg.bitrate_mbps = (std::min)(60, g_cfg.bitrate_mbps + 2);
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // System Audio toggle
        if (hit_test(g_rects.btnAudio, x, y)) {
            g_cfg.audio_enabled = !g_cfg.audio_enabled;
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Discord Isolation toggle
        if (hit_test(g_rects.btnDiscord, x, y)) {
            g_cfg.exclude_discord = !g_cfg.exclude_discord;
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Copy link
        if (hit_test(g_rects.btnCopy, x, y)) {
            std::string view_url = g_cfg.server_url;
            while (!view_url.empty() && view_url.back() == '/') view_url.pop_back();
            view_url += "/r/" + g_cfg.room_id;
            copy_text_to_clipboard(hwnd, view_url);
            g_copied_feedback = true;
            g_copied_tick = GetTickCount();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Regen room
        if (hit_test(g_rects.btnNewRoom, x, y)) {
            g_cfg.room_id = generate_random_room();
            if (g_state.is_streaming) start_stream_process();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Open Web link in browser
        if (hit_test(g_rects.btnOpenWeb, x, y)) {
            std::string view_url = g_cfg.server_url;
            while (!view_url.empty() && view_url.back() == '/') view_url.pop_back();
            view_url += "/r/" + g_cfg.room_id;
            ShellExecuteW(nullptr, L"open", to_wide(view_url).c_str(), nullptr, nullptr, SW_SHOW);
            return 0;
        }

        // Edit server URL dialog
        if (hit_test(g_rects.boxServer, x, y)) {
            std::string clip_text;
            if (OpenClipboard(hwnd)) {
                HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                if (hData) {
                    wchar_t* pText = (wchar_t*)GlobalLock(hData);
                    if (pText) {
                        std::wstring ws(pText);
                        clip_text = to_utf8(ws);
                        GlobalUnlock(hData);
                    }
                }
                CloseClipboard();
            }

            while (!clip_text.empty() && (clip_text.back() == '\r' || clip_text.back() == '\n' || clip_text.back() == ' ')) clip_text.pop_back();
            while (!clip_text.empty() && clip_text.front() == ' ') clip_text.erase(clip_text.begin());

            std::wstring cfg_path = get_app_dir() + L"\\server.txt";

            if (clip_text.rfind("http://", 0) == 0 || clip_text.rfind("https://", 0) == 0) {
                std::wstring msg = L"Do you want to set Server URL to:\n\n" + to_wide(clip_text) + L"\n\n(Click 'No' to open server.txt in Notepad)";
                int res = MessageBoxW(hwnd, msg.c_str(), L"Ghost Share Jor — Server URL", MB_YESNOCANCEL | MB_ICONQUESTION);
                if (res == IDYES) {
                    g_cfg.server_url = clip_text;
                    save_config();
                    if (g_state.is_streaming) start_stream_process();
                } else if (res == IDNO) {
                    ShellExecuteW(nullptr, L"open", L"notepad.exe", cfg_path.c_str(), nullptr, SW_SHOW);
                }
            } else {
                int res = MessageBoxW(hwnd, L"Click 'OK' to open server.txt in Notepad and enter your Server / Domain URL.\n\n(Tip: You can also copy an http/https URL and click here to paste it instantly!)", L"Ghost Share Jor — Server URL", MB_OKCANCEL | MB_ICONINFORMATION);
                if (res == IDOK) {
                    ShellExecuteW(nullptr, L"open", L"notepad.exe", cfg_path.c_str(), nullptr, SW_SHOW);
                }
            }
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

    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 16.5f, L"en-US", &g_tf_title);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.5f, L"en-US", &g_tf_title_center);
    g_tf_title_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_title_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-US", &g_tf_bold);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-US", &g_tf_regular);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &g_tf_small);
    g_dw->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-US", &g_tf_mono);

    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-US", &g_tf_btn_center);
    g_tf_btn_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_btn_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-US", &g_tf_ctrl_center);
    g_tf_ctrl_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_ctrl_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-US", &g_tf_section_lbl);

    g_dw->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-US", &g_tf_badge_center);
    g_tf_badge_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_badge_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

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
        L"GHOST SHARE JOR",
        WS_POPUP | WS_VISIBLE,
        wx, wy, WINDOW_WIDTH, WINDOW_HEIGHT,
        nullptr, nullptr, hInstance, nullptr
    );

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
    if (g_tf_title_center) g_tf_title_center->Release();
    if (g_tf_bold) g_tf_bold->Release();
    if (g_tf_regular) g_tf_regular->Release();
    if (g_tf_small) g_tf_small->Release();
    if (g_tf_mono) g_tf_mono->Release();
    if (g_tf_btn_center) g_tf_btn_center->Release();
    if (g_tf_ctrl_center) g_tf_ctrl_center->Release();
    if (g_tf_section_lbl) g_tf_section_lbl->Release();
    if (g_tf_badge_center) g_tf_badge_center->Release();
    if (g_rt) g_rt->Release();
    if (g_d2d) g_d2d->Release();
    if (g_dw) g_dw->Release();
    if (g_job) CloseHandle(g_job);

    return 0;
}
