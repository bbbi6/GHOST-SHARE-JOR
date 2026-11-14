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
#include <algorithm>
#include <cstdio>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include "ghost_share_core.h"
#include "theme.h"

static const int WINDOW_WIDTH = 720;
static const int WINDOW_HEIGHT = 440;

// Direct2D / DirectWrite resources
static HWND g_hwnd = nullptr;
static ID2D1Factory* g_d2d = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static IDWriteFactory* g_dw = nullptr;
static LumenTheme::FontSet g_fonts;

static IDWriteTextFormat* g_tf_brand = nullptr;
static IDWriteTextFormat* g_tf_body = nullptr;
static IDWriteTextFormat* g_tf_body_center = nullptr;
static IDWriteTextFormat* g_tf_mono = nullptr;
static IDWriteTextFormat* g_tf_mono_sm = nullptr;
static IDWriteTextFormat* g_tf_mono_center = nullptr;
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

enum class HoverTarget {
    None = 0,
    Close,
    Minimize,
    Server,
    Res0, Res1, Res2, Res3,
    Fps0, Fps1,
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
    D2D1_RECT_F bitrateTrack;

    D2D1_RECT_F btnAudio;
    D2D1_RECT_F btnDiscord;

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

static std::wstring to_upper_w(std::wstring s) {
    for (wchar_t& ch : s) ch = towupper(ch);
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
        g_state.status_text = "STANDBY";
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
        g_state.status_text = "CONNECTING";
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
            g_state.status_text = "ERROR";
            g_state.error_msg = "streaming runtime is missing from the app folder.";
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
            g_state.status_text = "ERROR";
            g_state.error_msg = "could not start bundled screen capture.";
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
            g_state.status_text = "LIVE";
        }
        if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);

        std::string tail = read_log_tail(log_path);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_stream_proc = nullptr;
            bool unexpected = (g_state.status_text == "LIVE" || g_state.status_text == "CONNECTING");
            g_state.is_streaming = false;
            if (unexpected) {
                g_state.status_text = "ENDED";
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
    float t = (x - g_rects.bitrateTrack.left) / (std::max)(1.0f, g_rects.bitrateTrack.right - g_rects.bitrateTrack.left);
    t = std::clamp(t, 0.0f, 1.0f);
    int raw = 2 + static_cast<int>(t * 58.0f + 0.5f);
    int snapped = ((raw + 1) / 2) * 2;
    return std::clamp(snapped, 2, 60);
}

static void draw_focus_rect(ID2D1RenderTarget* rt, const D2D1_RECT_F& rc, ID2D1Brush* bFocus) {
    D2D1_RECT_F out = D2D1::RectF(rc.left - 2.0f, rc.top - 2.0f, rc.right + 2.0f, rc.bottom + 2.0f);
    rt->DrawRectangle(out, bFocus, 1.0f);
}

static void render() {
    if (!g_rt) return;

    ShareConfig cfg;
    ShareState st;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        cfg = g_cfg;
        st = g_state;
    }

    g_rt->BeginDraw();
    g_rt->Clear(LumenTheme::color(LumenTheme::PAPER));

    ID2D1SolidColorBrush* bPaper2 = nullptr;
    ID2D1SolidColorBrush* bPaper3 = nullptr;
    ID2D1SolidColorBrush* bInk = nullptr;
    ID2D1SolidColorBrush* bInk2 = nullptr;
    ID2D1SolidColorBrush* bMuted = nullptr;
    ID2D1SolidColorBrush* bRule = nullptr;
    ID2D1SolidColorBrush* bRule2 = nullptr;
    ID2D1SolidColorBrush* bBlueprint = nullptr;
    ID2D1SolidColorBrush* bAccent = nullptr;
    ID2D1SolidColorBrush* bAccentEmit = nullptr;
    ID2D1SolidColorBrush* bCoral = nullptr;

    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::PAPER_2), &bPaper2);
    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::PAPER_3), &bPaper3);
    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::INK), &bInk);
    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::INK_2), &bInk2);
    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::MUTED), &bMuted);
    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::INK, LumenTheme::RULE_ALPHA), &bRule);
    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::INK, LumenTheme::RULE_2_ALPHA), &bRule2);
    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::INK, LumenTheme::BLUEPRINT_ALPHA), &bBlueprint);
    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::ACCENT), &bAccent);
    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::ACCENT, LumenTheme::PAPER_EMIT_ALPHA), &bAccentEmit);
    g_rt->CreateSolidColorBrush(LumenTheme::color(LumenTheme::ACCENT_2), &bCoral);

    // Outer 1px frame
    g_rt->DrawRectangle(D2D1::RectF(0.5f, 0.5f, WINDOW_WIDTH - 0.5f, WINDOW_HEIGHT - 0.5f), bRule2, 1.0f);

    // --- TOP RACK BAR (y: 0..46) ---
    g_rt->FillRectangle(D2D1::RectF(1.0f, 1.0f, WINDOW_WIDTH - 1.0f, 46.0f), bPaper2);
    g_rt->DrawLine(D2D1::Point2F(0.0f, 46.5f), D2D1::Point2F((float)WINDOW_WIDTH, 46.5f), bRule, 1.0f);

    // Brand lowercase + status emitter
    d2d_draw(g_rt, L"ghost share jor", g_tf_brand, D2D1::RectF(20.0f, 12.0f, 165.0f, 34.0f), bInk);
    g_rt->DrawLine(D2D1::Point2F(168.5f, 14.0f), D2D1::Point2F(168.5f, 32.0f), bRule, 1.0f);

    bool isLive = (st.is_streaming && st.status_text == "LIVE");
    bool isError = (!st.error_msg.empty());
    ID2D1Brush* emitterBrush = isLive ? bAccent : (isError ? bCoral : bMuted);
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(184.0f, 23.0f), 3.0f, 3.0f), emitterBrush);

    std::wstring statusStr = to_upper_w(to_wide(st.status_text.empty() ? "STANDBY" : st.status_text));
    if (st.is_streaming) {
        uint32_t sec = (GetTickCount() - st.start_time) / 1000;
        wchar_t tbuf[32];
        swprintf_s(tbuf, L" · %02u:%02u", sec / 60, sec % 60);
        statusStr += tbuf;
    }
    d2d_draw(g_rt, statusStr, g_tf_mono_sm, D2D1::RectF(196.0f, 16.0f, 360.0f, 34.0f), isLive ? bAccent : bMuted);

    // Server chip
    g_rects.boxServer = D2D1::RectF(368.0f, 10.0f, 628.0f, 36.0f);
    bool srvHover = (g_hover == HoverTarget::Server);
    g_rt->FillRectangle(g_rects.boxServer, srvHover ? bPaper3 : bPaper2);
    g_rt->DrawRectangle(g_rects.boxServer, srvHover ? bRule2 : bRule, 1.0f);
    std::string shortSrv = cfg.server_url;
    if (shortSrv.size() > 28) shortSrv = shortSrv.substr(0, 26) + "..";
    std::wstring srvLbl = L"SRV · " + to_upper_w(to_wide(shortSrv));
    d2d_draw(g_rt, srvLbl, g_tf_mono_sm, D2D1::RectF(378.0f, 16.0f, 620.0f, 32.0f), srvHover ? bInk : bInk2);
    if (g_focus == HoverTarget::Server) draw_focus_rect(g_rt, g_rects.boxServer, bAccent);

    // Window controls
    g_rects.btnMinimize = D2D1::RectF(638.0f, 10.0f, 666.0f, 36.0f);
    g_rects.btnClose    = D2D1::RectF(672.0f, 10.0f, 700.0f, 36.0f);
    if (g_hover == HoverTarget::Minimize) g_rt->FillRectangle(g_rects.btnMinimize, bPaper3);
    if (g_hover == HoverTarget::Close)    g_rt->FillRectangle(g_rects.btnClose, bCoral);
    d2d_draw(g_rt, L"—", g_tf_mono_center, g_rects.btnMinimize, bInk2);
    d2d_draw(g_rt, L"✕", g_tf_mono_center, g_rects.btnClose, (g_hover == HoverTarget::Close) ? bInk : bInk2);

    // --- LEFT PANE: SIGNAL CHAIN APPARATUS (x: 20..268, y: 60..328) ---
    D2D1_RECT_F appBox = D2D1::RectF(20.0f, 60.0f, 268.0f, 328.0f);
    g_rt->FillRectangle(appBox, bPaper2);
    g_rt->DrawRectangle(appBox, bRule, 1.0f);

    // Subtle blueprint grid inside schematic box
    g_rt->PushAxisAlignedClip(appBox, D2D1_ANTIALIAS_MODE_ALIASED);
    for (float gx = appBox.left + 24.0f; gx < appBox.right; gx += 24.0f) {
        g_rt->DrawLine(D2D1::Point2F(gx, appBox.top), D2D1::Point2F(gx, appBox.bottom), bBlueprint, 1.0f);
    }
    for (float gy = appBox.top + 24.0f; gy < appBox.bottom; gy += 24.0f) {
        g_rt->DrawLine(D2D1::Point2F(appBox.left, gy), D2D1::Point2F(appBox.right, gy), bBlueprint, 1.0f);
    }
    g_rt->PopAxisAlignedClip();

    d2d_draw(g_rt, L"SIGNAL CHAIN", g_tf_mono_sm, D2D1::RectF(34.0f, 72.0f, 200.0f, 88.0f), bMuted);

    // 4 schematic nodes vertically connected
    const wchar_t* nodeNames[4] = { L"CAPTURE", L"ENCODE", L"AUDIO", L"ROOM" };
    std::wstring resVal = to_upper_w(resolution_title(cfg.resolution));
    wchar_t fpsBuf[16];
    swprintf_s(fpsBuf, L"%d FPS", (int)cfg.fps);
    std::wstring encVal = (cfg.use_nvenc ? L"NVENC · " : L"X264 · ") + std::to_wstring(cfg.bitrate_mbps) + L"M";
    DWORD discordPid = cfg.exclude_discord ? find_discord_root_pid() : 0;
    std::wstring audVal = !cfg.audio_enabled ? L"MUTED"
        : (cfg.exclude_discord
            ? (discordPid ? (L"EXCL PID " + std::to_wstring(discordPid)) : L"EXCL · NO DISC")
            : L"SYS + DISCORD");
    std::wstring roomVal = to_upper_w(to_wide(cfg.room_id));

    std::wstring nodeVals[4] = {
        resVal + L" · " + fpsBuf,
        encVal,
        audVal,
        roomVal
    };

    for (int i = 0; i < 4; ++i) {
        float ny = 104.0f + i * 54.0f;
        D2D1_RECT_F nRect = D2D1::RectF(34.0f, ny, 108.0f, ny + 30.0f);
        bool activeNode = (i == 3 && isLive);
        g_rt->FillRectangle(nRect, activeNode ? bAccentEmit : bPaper3);
        g_rt->DrawRectangle(nRect, activeNode ? bAccent : bRule2, 1.0f);
        d2d_draw(g_rt, nodeNames[i], g_tf_mono_center, nRect, activeNode ? bAccent : bInk2);

        // Leader line to live readout
        g_rt->DrawLine(D2D1::Point2F(108.0f, ny + 15.0f), D2D1::Point2F(124.0f, ny + 15.0f), activeNode ? bAccent : bRule2, 1.0f);
        d2d_draw(g_rt, nodeVals[i], g_tf_mono_sm, D2D1::RectF(130.0f, ny + 8.0f, 258.0f, ny + 26.0f), activeNode ? bAccent : bInk);

        // Vertical bus connector to next node
        if (i < 3) {
            g_rt->DrawLine(D2D1::Point2F(71.0f, ny + 30.0f), D2D1::Point2F(71.0f, ny + 54.0f), isLive ? bAccent : bRule2, 1.0f);
        }
    }

    // --- RIGHT PANE: CONTROLS (x: 284..700) ---

    // 1. SOURCE RESOLUTION (y: 60..118)
    d2d_draw(g_rt, L"SOURCE", g_tf_mono_sm, D2D1::RectF(284.0f, 62.0f, 380.0f, 78.0f), bMuted);
    const wchar_t* resLabels[4] = { L"native", L"1440p", L"1080p", L"720p" };
    float segW = (700.0f - 284.0f) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        g_rects.chipRes[i] = D2D1::RectF(284.0f + i * segW, 82.0f, 284.0f + (i + 1) * segW, 114.0f);
        bool sel = ((int)cfg.resolution == i);
        bool hov = (g_hover == (HoverTarget)((int)HoverTarget::Res0 + i));
        g_rt->FillRectangle(g_rects.chipRes[i], sel ? bAccentEmit : (hov ? bPaper3 : bPaper2));
        g_rt->DrawRectangle(g_rects.chipRes[i], bRule, 1.0f);
        if (sel) {
            g_rt->DrawLine(
                D2D1::Point2F(g_rects.chipRes[i].left, g_rects.chipRes[i].bottom - 1.0f),
                D2D1::Point2F(g_rects.chipRes[i].right, g_rects.chipRes[i].bottom - 1.0f),
                bAccent, 2.0f
            );
        }
        d2d_draw(g_rt, resLabels[i], g_tf_body_center, g_rects.chipRes[i], sel ? bAccent : (hov ? bInk : bInk2));
        if (g_focus == (HoverTarget)((int)HoverTarget::Res0 + i)) draw_focus_rect(g_rt, g_rects.chipRes[i], bAccent);
    }

    // 2. RATE & BITRATE TICK SLIDER (y: 128..236)
    d2d_draw(g_rt, L"FRAME RATE", g_tf_mono_sm, D2D1::RectF(284.0f, 130.0f, 390.0f, 146.0f), bMuted);
    d2d_draw(g_rt, L"BITRATE · 2–60 MBPS", g_tf_mono_sm, D2D1::RectF(416.0f, 130.0f, 600.0f, 146.0f), bMuted);
    std::wstring mbpsReadout = std::to_wstring(cfg.bitrate_mbps) + L" MBPS";
    IDWriteTextFormat* oldAlign = g_tf_mono_sm;
    d2d_draw(g_rt, mbpsReadout, g_tf_mono, D2D1::RectF(630.0f, 128.0f, 700.0f, 146.0f), bAccent);
    (void)oldAlign;

    const wchar_t* fpsLabels[2] = { L"60 fps", L"30 fps" };
    for (int i = 0; i < 2; ++i) {
        g_rects.chipFps[i] = D2D1::RectF(284.0f + i * 60.0f, 152.0f, 284.0f + (i + 1) * 60.0f, 184.0f);
        bool sel = (i == 0 && cfg.fps == ScreenFps::Fps60) || (i == 1 && cfg.fps == ScreenFps::Fps30);
        bool hov = (g_hover == (i == 0 ? HoverTarget::Fps0 : HoverTarget::Fps1));
        g_rt->FillRectangle(g_rects.chipFps[i], sel ? bAccentEmit : (hov ? bPaper3 : bPaper2));
        g_rt->DrawRectangle(g_rects.chipFps[i], bRule, 1.0f);
        if (sel) {
            g_rt->DrawLine(
                D2D1::Point2F(g_rects.chipFps[i].left, g_rects.chipFps[i].bottom - 1.0f),
                D2D1::Point2F(g_rects.chipFps[i].right, g_rects.chipFps[i].bottom - 1.0f),
                bAccent, 2.0f
            );
        }
        d2d_draw(g_rt, fpsLabels[i], g_tf_body_center, g_rects.chipFps[i], sel ? bAccent : (hov ? bInk : bInk2));
        if (g_focus == (i == 0 ? HoverTarget::Fps0 : HoverTarget::Fps1)) draw_focus_rect(g_rt, g_rects.chipFps[i], bAccent);
    }

    // Tick Slider well (x: 416..700, y: 152..184)
    g_rects.bitrateTrack = D2D1::RectF(416.0f, 152.0f, 700.0f, 184.0f);
    bool sliderHov = (g_hover == HoverTarget::BitrateSlider || g_dragging_bitrate);
    g_rt->FillRectangle(g_rects.bitrateTrack, bPaper2);
    g_rt->DrawRectangle(g_rects.bitrateTrack, sliderHov ? bRule2 : bRule, 1.0f);

    float trackPad = 12.0f;
    float trackLeft = g_rects.bitrateTrack.left + trackPad;
    float trackRight = g_rects.bitrateTrack.right - trackPad;
    float trackSpan = trackRight - trackLeft;
    float norm = (float)(cfg.bitrate_mbps - 2) / 58.0f;
    float needleX = trackLeft + norm * trackSpan;

    // 30 vertical ticks (every 2 Mbps)
    for (int t = 0; t < 30; ++t) {
        float tx = trackLeft + ((float)t / 29.0f) * trackSpan;
        int tickVal = 2 + t * 2;
        bool activeTick = (tickVal <= cfg.bitrate_mbps);
        float tickH = (t % 5 == 0) ? 14.0f : 8.0f;
        g_rt->DrawLine(
            D2D1::Point2F(tx, 178.0f - tickH),
            D2D1::Point2F(tx, 178.0f),
            activeTick ? bAccent : bRule2,
            1.0f
        );
    }
    // Needle marker
    g_rt->FillRectangle(D2D1::RectF(needleX - 1.5f, 156.0f, needleX + 1.5f, 180.0f), bInk);
    if (g_focus == HoverTarget::BitrateSlider) draw_focus_rect(g_rt, g_rects.bitrateTrack, bAccent);

    // 3. AUDIO & DISCORD ISOLATION (y: 202..276)
    d2d_draw(g_rt, L"AUDIO ROUTING", g_tf_mono_sm, D2D1::RectF(284.0f, 202.0f, 440.0f, 218.0f), bMuted);

    g_rects.btnAudio = D2D1::RectF(284.0f, 224.0f, 486.0f, 278.0f);
    bool audHov = (g_hover == HoverTarget::Audio);
    g_rt->FillRectangle(g_rects.btnAudio, cfg.audio_enabled ? bAccentEmit : (audHov ? bPaper3 : bPaper2));
    g_rt->DrawRectangle(g_rects.btnAudio, cfg.audio_enabled ? bAccent : (audHov ? bRule2 : bRule), 1.0f);
    d2d_draw(g_rt, L"system audio", g_tf_body, D2D1::RectF(296.0f, 234.0f, 440.0f, 254.0f), cfg.audio_enabled ? bInk : bInk2);
    d2d_draw(g_rt, cfg.audio_enabled ? L"ON" : L"OFF", g_tf_mono_sm, D2D1::RectF(446.0f, 236.0f, 476.0f, 252.0f), cfg.audio_enabled ? bAccent : bMuted);
    d2d_draw(g_rt, cfg.audio_enabled ? L"WASAPI LOOPBACK" : L"VIDEO ONLY", g_tf_mono_sm, D2D1::RectF(296.0f, 256.0f, 476.0f, 272.0f), bMuted);
    if (g_focus == HoverTarget::Audio) draw_focus_rect(g_rt, g_rects.btnAudio, bAccent);

    g_rects.btnDiscord = D2D1::RectF(498.0f, 224.0f, 700.0f, 278.0f);
    bool discHov = (g_hover == HoverTarget::Discord);
    bool discActive = (cfg.audio_enabled && cfg.exclude_discord);
    g_rt->FillRectangle(g_rects.btnDiscord, discActive ? bAccentEmit : (discHov ? bPaper3 : bPaper2));
    g_rt->DrawRectangle(g_rects.btnDiscord, discActive ? bAccent : (discHov ? bRule2 : bRule), 1.0f);
    d2d_draw(g_rt, L"exclude discord", g_tf_body, D2D1::RectF(510.0f, 234.0f, 650.0f, 254.0f), discActive ? bInk : bInk2);
    d2d_draw(g_rt, discActive ? L"ON" : L"OFF", g_tf_mono_sm, D2D1::RectF(660.0f, 236.0f, 690.0f, 252.0f), discActive ? bAccent : bMuted);
    std::wstring discSub = !cfg.audio_enabled ? L"AUDIO DISABLED"
        : (discordPid ? (L"PID " + std::to_wstring(discordPid) + L" ISOLATED") : (cfg.exclude_discord ? L"WAITING FOR DISCORD" : L"INCLUDED IN MIX"));
    d2d_draw(g_rt, discSub, g_tf_mono_sm, D2D1::RectF(510.0f, 256.0f, 690.0f, 272.0f), bMuted);
    if (g_focus == HoverTarget::Discord) draw_focus_rect(g_rt, g_rects.btnDiscord, bAccent);

    // 4. VIEWER LINK STRIP (y: 292..328)
    D2D1_RECT_F linkWell = D2D1::RectF(284.0f, 294.0f, 700.0f, 328.0f);
    g_rt->FillRectangle(linkWell, bPaper2);
    g_rt->DrawRectangle(linkWell, bRule, 1.0f);

    std::string viewUrl = cfg.server_url;
    while (!viewUrl.empty() && viewUrl.back() == '/') viewUrl.pop_back();
    viewUrl += "/r/" + cfg.room_id;
    std::string dispUrl = viewUrl;
    if (dispUrl.size() > 34) dispUrl = ".." + dispUrl.substr(dispUrl.size() - 32);
    d2d_draw(g_rt, to_wide(dispUrl), g_tf_mono_sm, D2D1::RectF(296.0f, 304.0f, 520.0f, 322.0f), bInk2);

    g_rects.btnCopy    = D2D1::RectF(528.0f, 299.0f, 580.0f, 323.0f);
    g_rects.btnNewRoom = D2D1::RectF(584.0f, 299.0f, 640.0f, 323.0f);
    g_rects.btnOpenWeb = D2D1::RectF(644.0f, 299.0f, 694.0f, 323.0f);

    auto draw_mini_btn = [&](const D2D1_RECT_F& r, const wchar_t* lbl, HoverTarget t, bool accentText = false) {
        bool h = (g_hover == t);
        g_rt->FillRectangle(r, h ? bPaper3 : bPaper2);
        g_rt->DrawRectangle(r, accentText ? bAccent : (h ? bRule2 : bRule), 1.0f);
        d2d_draw(g_rt, lbl, g_tf_body_center, r, accentText ? bAccent : (h ? bInk : bInk2));
        if (g_focus == t) draw_focus_rect(g_rt, r, bAccent);
    };
    draw_mini_btn(g_rects.btnCopy, g_copied_feedback ? L"copied" : L"copy", HoverTarget::Copy, g_copied_feedback);
    draw_mini_btn(g_rects.btnNewRoom, L"new", HoverTarget::NewRoom);
    draw_mini_btn(g_rects.btnOpenWeb, L"open", HoverTarget::OpenWeb);

    // --- FOOTER RACK BAR (y: 344..422) ---
    g_rt->DrawLine(D2D1::Point2F(20.0f, 344.5f), D2D1::Point2F(700.0f, 344.5f), bRule, 1.0f);

    // Primary Action Button
    g_rects.btnStartStop = D2D1::RectF(536.0f, 358.0f, 700.0f, 396.0f);
    bool actHov = (g_hover == HoverTarget::StartStop);
    ID2D1SolidColorBrush* actBorder = st.is_streaming ? bCoral : bAccent;
    g_rt->FillRectangle(g_rects.btnStartStop, actHov ? bPaper3 : (st.is_streaming ? bPaper2 : bAccentEmit));
    g_rt->DrawRectangle(g_rects.btnStartStop, actBorder, 1.0f);
    d2d_draw(g_rt, st.is_streaming ? L"end broadcast" : L"go live", g_tf_btn, g_rects.btnStartStop, st.is_streaming ? bCoral : bAccent);
    if (g_focus == HoverTarget::StartStop) draw_focus_rect(g_rt, g_rects.btnStartStop, bAccent);

    // Error / Keyboard hint line
    if (!st.error_msg.empty()) {
        d2d_draw(g_rt, to_wide(st.error_msg), g_tf_mono_sm, D2D1::RectF(20.0f, 366.0f, 520.0f, 412.0f), bCoral);
    } else {
        d2d_draw(g_rt, L"SPACE: GO LIVE  ·  TAB: FOCUS  ·  1–4: RES  ·  CTRL+C: COPY LINK", g_tf_mono_sm, D2D1::RectF(20.0f, 370.0f, 520.0f, 390.0f), bMuted);
    }

    g_rt->EndDraw();

    bPaper2->Release();
    bPaper3->Release();
    bInk->Release();
    bInk2->Release();
    bMuted->Release();
    bRule->Release();
    bRule2->Release();
    bBlueprint->Release();
    bAccent->Release();
    bAccentEmit->Release();
    bCoral->Release();
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
    if (hit_test(g_rects.bitrateTrack, x, y)) return HoverTarget::BitrateSlider;
    if (hit_test(g_rects.btnAudio, x, y)) return HoverTarget::Audio;
    if (hit_test(g_rects.btnDiscord, x, y)) return HoverTarget::Discord;
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
        int res = MessageBoxW(hwnd, msg.c_str(), L"ghost share jor — server url", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (res == IDYES) {
            g_cfg.server_url = clip_text;
            save_config();
            if (g_state.is_streaming) start_stream_process();
        } else if (res == IDNO) {
            ShellExecuteW(nullptr, L"open", L"notepad.exe", cfg_path.c_str(), nullptr, SW_SHOW);
        }
    } else {
        int res = MessageBoxW(hwnd, L"Click 'OK' to open server.txt in Notepad.\n\n(Tip: Copy an http/https URL first and click here to apply it directly.)", L"ghost share jor — server url", MB_OKCANCEL | MB_ICONINFORMATION);
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
    case HoverTarget::Res3: {
        g_cfg.resolution = (ScreenResolution)((int)target - (int)HoverTarget::Res0);
        if (g_state.is_streaming) start_stream_process();
        break;
    }
    case HoverTarget::Fps0:
        g_cfg.fps = ScreenFps::Fps60;
        if (g_state.is_streaming) start_stream_process();
        break;
    case HoverTarget::Fps1:
        g_cfg.fps = ScreenFps::Fps30;
        if (g_state.is_streaming) start_stream_process();
        break;
    case HoverTarget::Audio:
        g_cfg.audio_enabled = !g_cfg.audio_enabled;
        if (g_state.is_streaming) start_stream_process();
        break;
    case HoverTarget::Discord:
        g_cfg.exclude_discord = !g_cfg.exclude_discord;
        if (g_state.is_streaming) start_stream_process();
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
    HoverTarget::Fps0, HoverTarget::Fps1, HoverTarget::BitrateSlider,
    HoverTarget::Audio, HoverTarget::Discord,
    HoverTarget::Copy, HoverTarget::NewRoom, HoverTarget::OpenWeb,
    HoverTarget::StartStop, HoverTarget::Server
};

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (pt.y < 46 && !hit_test(g_rects.btnClose, (float)pt.x, (float)pt.y)
            && !hit_test(g_rects.btnMinimize, (float)pt.x, (float)pt.y)
            && !hit_test(g_rects.boxServer, (float)pt.x, (float)pt.y)) {
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
    g_fonts = LumenTheme::load_fonts(g_dw);

    const wchar_t* bodyF = g_fonts.bodyFamily.c_str();
    const wchar_t* monoF = g_fonts.monoFamily.c_str();
    IDWriteFontCollection* col = g_fonts.collection;

    g_dw->CreateTextFormat(bodyF, col, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.5f, L"en-US", &g_tf_brand);
    g_dw->CreateTextFormat(bodyF, col, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"en-US", &g_tf_body);
    g_dw->CreateTextFormat(bodyF, col, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-US", &g_tf_body_center);
    g_tf_body_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_body_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(monoF, col, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-US", &g_tf_mono);
    g_dw->CreateTextFormat(monoF, col, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-US", &g_tf_mono_sm);
    g_dw->CreateTextFormat(monoF, col, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, L"en-US", &g_tf_mono_center);
    g_tf_mono_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_mono_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(bodyF, col, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-US", &g_tf_btn);
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
        L"ghost share jor",
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

    if (g_tf_brand) g_tf_brand->Release();
    if (g_tf_body) g_tf_body->Release();
    if (g_tf_body_center) g_tf_body_center->Release();
    if (g_tf_mono) g_tf_mono->Release();
    if (g_tf_mono_sm) g_tf_mono_sm->Release();
    if (g_tf_mono_center) g_tf_mono_center->Release();
    if (g_tf_btn) g_tf_btn->Release();
    g_fonts.release();
    if (g_rt) g_rt->Release();
    if (g_d2d) g_d2d->Release();
    if (g_dw) g_dw->Release();
    if (g_job) CloseHandle(g_job);

    return 0;
}
