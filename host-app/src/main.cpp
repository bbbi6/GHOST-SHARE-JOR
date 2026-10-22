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
static const int WINDOW_HEIGHT = 650;

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
static IDWriteTextFormat* g_tf_chip = nullptr;

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

static std::wstring resolve_gst_bin(std::wstring& out_bin_dir) {
    std::wstring app_dir = get_app_dir();
    
    // 1. App directory / bin
    std::wstring c1 = app_dir + L"\\bin\\gst-launch-1.0.exe";
    if (GetFileAttributesW(c1.c_str()) != INVALID_FILE_ATTRIBUTES) {
        out_bin_dir = app_dir + L"\\bin";
        return c1;
    }
    
    // 2. App directory root
    std::wstring c2 = app_dir + L"\\gst-launch-1.0.exe";
    if (GetFileAttributesW(c2.c_str()) != INVALID_FILE_ATTRIBUTES) {
        out_bin_dir = app_dir;
        return c2;
    }

    // 3. MSYS2 UCRT64 default
    std::wstring c3 = L"C:\\msys64\\ucrt64\\bin\\gst-launch-1.0.exe";
    if (GetFileAttributesW(c3.c_str()) != INVALID_FILE_ATTRIBUTES) {
        out_bin_dir = L"C:\\msys64\\ucrt64\\bin";
        return c3;
    }

    // 4. Fallback in PATH
    out_bin_dir = L"";
    return L"gst-launch-1.0.exe";
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

        wchar_t old_path[32767] = {};
        GetEnvironmentVariableW(L"PATH", old_path, 32767);
        if (!bin_dir.empty()) {
            std::wstring new_path = bin_dir + L";" + std::wstring(old_path);
            SetEnvironmentVariableW(L"PATH", new_path.c_str());
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
            g_state.error_msg = "Failed to launch gst-launch-1.0.exe";
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

        // Wait for process to exit
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);

        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_stream_proc = nullptr;
            g_state.is_streaming = false;
            if (g_state.status_text.find("LIVE") != std::string::npos) {
                g_state.status_text = "Stream ended";
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
    float eye_r = r * 0.18f;
    D2D1_ELLIPSE eyeL = D2D1::Ellipse(D2D1::Point2F(cx - r * 0.35f, cy - r * 0.2f), eye_r, eye_r * 1.3f);
    D2D1_ELLIPSE eyeR = D2D1::Ellipse(D2D1::Point2F(cx + r * 0.35f, cy - r * 0.2f), eye_r, eye_r * 1.3f);
    rt->FillEllipse(eyeL, eyeBrush);
    rt->FillEllipse(eyeR, eyeBrush);
}

static void render() {
    if (!g_rt) return;
    g_rt->BeginDraw();
    g_rt->Clear(D2D1::ColorF(0x09090b)); // Dark Zinc Obsidian

    ID2D1SolidColorBrush* bCard = nullptr;
    ID2D1SolidColorBrush* bText = nullptr;
    ID2D1SolidColorBrush* bMuted = nullptr;
    ID2D1SolidColorBrush* bAccent = nullptr;
    ID2D1SolidColorBrush* bAccentBg = nullptr;
    ID2D1SolidColorBrush* bRed = nullptr;
    ID2D1SolidColorBrush* bGreen = nullptr;
    ID2D1SolidColorBrush* bBorder = nullptr;
    ID2D1SolidColorBrush* bBg = nullptr;

    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x18181b), &bCard);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0xfafafa), &bText);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x71717a), &bMuted);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x6366f1), &bAccent); // Indigo 500
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x312e81), &bAccentBg); // Indigo 900
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0xef4444), &bRed);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x10b981), &bGreen);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x27272a), &bBorder);
    g_rt->CreateSolidColorBrush(D2D1::ColorF(0x09090b), &bBg);

    // Header & Emblem
    draw_ghost_emblem(g_rt, 36.0f, 32.0f, 15.0f, bAccent, bBg);

    D2D1_RECT_F rTitle = D2D1::RectF(62, 14, 400, 38);
    d2d_draw(g_rt, L"GHOST SHARE JOR", g_tf_title, rTitle, bText);

    D2D1_RECT_F rSub = D2D1::RectF(62, 36, 480, 54);
    d2d_draw(g_rt, L"HAIL THAN TAI \u2022 LOW LATENCY SCREEN SHARING", g_tf_small, rSub, bMuted);

    // Window controls (Min / Close)
    g_rects.btnMinimize = D2D1::RectF(WINDOW_WIDTH - 64, 12, WINDOW_WIDTH - 38, 38);
    g_rects.btnClose = D2D1::RectF(WINDOW_WIDTH - 34, 12, WINDOW_WIDTH - 8, 38);
    d2d_draw(g_rt, L"\u2014", g_tf_ctrl_center, g_rects.btnMinimize, bMuted);
    d2d_draw(g_rt, L"\u2715", g_tf_ctrl_center, g_rects.btnClose, bMuted);

    // Server URL Card
    D2D1_ROUNDED_RECT rcServer = D2D1::RoundedRect(D2D1::RectF(24, 62, WINDOW_WIDTH - 24, 110), 8, 8);
    g_rects.boxServer = rcServer.rect;
    g_rt->FillRoundedRectangle(rcServer, bCard);
    g_rt->DrawRoundedRectangle(rcServer, bBorder, 1.0f);

    D2D1_RECT_F rSrvLbl = D2D1::RectF(36, 68, 380, 84);
    d2d_draw(g_rt, L"SERVER URL (Click to Edit):", g_tf_section_lbl, rSrvLbl, bMuted);
    D2D1_RECT_F rSrvVal = D2D1::RectF(36, 84, WINDOW_WIDTH - 36, 106);
    std::wstring wsServer = to_wide(g_cfg.server_url);
    d2d_draw(g_rt, wsServer, g_tf_bold, rSrvVal, bText);

    // -------------------------------------------------------------
    // SECTION 1: RESOLUTION (4 Visual Chips)
    // -------------------------------------------------------------
    D2D1_RECT_F rResLbl = D2D1::RectF(24, 120, WINDOW_WIDTH - 24, 136);
    d2d_draw(g_rt, L"RESOLUTION (ความละเอียดจอ):", g_tf_section_lbl, rResLbl, bMuted);

    const wchar_t* res_names[4] = { L"Source (Native)", L"1440p (QHD)", L"1080p (FHD)", L"720p (HD)" };
    float total_usable_w = (float)(WINDOW_WIDTH - 48); // 512
    float chip_w = (total_usable_w - (3 * 8)) / 4.0f; // 122 px
    float chip_y = 138.0f;
    float chip_h = 34.0f;

    for (int i = 0; i < 4; ++i) {
        float x1 = 24.0f + i * (chip_w + 8.0f);
        float x2 = x1 + chip_w;
        g_rects.chipRes[i] = D2D1::RectF(x1, chip_y, x2, chip_y + chip_h);
        D2D1_ROUNDED_RECT rcChip = D2D1::RoundedRect(g_rects.chipRes[i], 6, 6);

        bool is_active = ((int)g_cfg.resolution == i);
        if (is_active) {
            g_rt->FillRoundedRectangle(rcChip, bAccent);
            d2d_draw(g_rt, res_names[i], g_tf_btn_center, g_rects.chipRes[i], bText);
        } else {
            g_rt->FillRoundedRectangle(rcChip, bCard);
            g_rt->DrawRoundedRectangle(rcChip, bBorder, 1.0f);
            d2d_draw(g_rt, res_names[i], g_tf_btn_center, g_rects.chipRes[i], bMuted);
        }
    }

    // -------------------------------------------------------------
    // SECTION 2: FRAMERATE & BITRATE CONTROLS
    // -------------------------------------------------------------
    D2D1_RECT_F rFpsBitLbl = D2D1::RectF(24, 184, WINDOW_WIDTH - 24, 200);
    d2d_draw(g_rt, L"FRAMERATE & BITRATE (เฟรมเรต & บิตเรต):", g_tf_section_lbl, rFpsBitLbl, bMuted);

    float row2_y = 202.0f;
    float row2_h = 34.0f;

    // FPS Chips (60 FPS & 30 FPS)
    const wchar_t* fps_names[2] = { L"60 FPS", L"30 FPS" };
    float fps_w = 72.0f;
    for (int i = 0; i < 2; ++i) {
        float x1 = 24.0f + i * (fps_w + 6.0f);
        float x2 = x1 + fps_w;
        g_rects.chipFps[i] = D2D1::RectF(x1, row2_y, x2, row2_y + row2_h);
        D2D1_ROUNDED_RECT rcFps = D2D1::RoundedRect(g_rects.chipFps[i], 6, 6);

        bool is_active = (i == 0 && g_cfg.fps == ScreenFps::Fps60) || (i == 1 && g_cfg.fps == ScreenFps::Fps30);
        if (is_active) {
            g_rt->FillRoundedRectangle(rcFps, bAccent);
            d2d_draw(g_rt, fps_names[i], g_tf_btn_center, g_rects.chipFps[i], bText);
        } else {
            g_rt->FillRoundedRectangle(rcFps, bCard);
            g_rt->DrawRoundedRectangle(rcFps, bBorder, 1.0f);
            d2d_draw(g_rt, fps_names[i], g_tf_btn_center, g_rects.chipFps[i], bMuted);
        }
    }

    // Bitrate Popular Chips (8M, 12M, 16M, 25M, 40M)
    float bit_start_x = 188.0f;
    float bit_w = 46.0f;
    for (int i = 0; i < 5; ++i) {
        float x1 = bit_start_x + i * (bit_w + 5.0f);
        float x2 = x1 + bit_w;
        g_rects.chipBitrate[i] = D2D1::RectF(x1, row2_y, x2, row2_y + row2_h);
        D2D1_ROUNDED_RECT rcBit = D2D1::RoundedRect(g_rects.chipBitrate[i], 6, 6);

        bool is_active = (g_cfg.bitrate_mbps == POPULAR_BITRATES[i]);
        wchar_t b_label[16];
        swprintf_s(b_label, L"%dM", POPULAR_BITRATES[i]);

        if (is_active) {
            g_rt->FillRoundedRectangle(rcBit, bAccent);
            d2d_draw(g_rt, b_label, g_tf_btn_center, g_rects.chipBitrate[i], bText);
        } else {
            g_rt->FillRoundedRectangle(rcBit, bCard);
            g_rt->DrawRoundedRectangle(rcBit, bBorder, 1.0f);
            d2d_draw(g_rt, b_label, g_tf_btn_center, g_rects.chipBitrate[i], bMuted);
        }
    }

    // Steppers: [-] and [+]
    g_rects.btnBitrateMinus = D2D1::RectF(448, row2_y, 488, row2_y + row2_h);
    g_rects.btnBitratePlus = D2D1::RectF(494, row2_y, 536, row2_y + row2_h);

    D2D1_ROUNDED_RECT rcMinus = D2D1::RoundedRect(g_rects.btnBitrateMinus, 6, 6);
    D2D1_ROUNDED_RECT rcPlus = D2D1::RoundedRect(g_rects.btnBitratePlus, 6, 6);
    g_rt->FillRoundedRectangle(rcMinus, bCard);
    g_rt->DrawRoundedRectangle(rcMinus, bBorder, 1.0f);
    d2d_draw(g_rt, L"\u2212", g_tf_ctrl_center, g_rects.btnBitrateMinus, bText);

    g_rt->FillRoundedRectangle(rcPlus, bCard);
    g_rt->DrawRoundedRectangle(rcPlus, bBorder, 1.0f);
    d2d_draw(g_rt, L"+", g_tf_ctrl_center, g_rects.btnBitratePlus, bText);

    // Current spec preview description
    D2D1_RECT_F rSpecPrev = D2D1::RectF(24, 240, WINDOW_WIDTH - 24, 256);
    wchar_t spec_desc[128];
    swprintf_s(spec_desc, L"Active Setting: %s @ %s \u2022 %d Mbps (Max Bitrate)",
               resolution_title(g_cfg.resolution), fps_title(g_cfg.fps), g_cfg.bitrate_mbps);
    d2d_draw(g_rt, spec_desc, g_tf_small, rSpecPrev, bAccent);

    // -------------------------------------------------------------
    // SECTION 3: AUDIO & DISCORD ISOLATION (Two Equal Cards)
    // -------------------------------------------------------------
    D2D1_RECT_F rAudioSectionLbl = D2D1::RectF(24, 266, WINDOW_WIDTH - 24, 282);
    d2d_draw(g_rt, L"AUDIO & DISCORD ISOLATION (ระบบเสียง & แยกเสียง):", g_tf_section_lbl, rAudioSectionLbl, bMuted);

    float card_audio_w = (total_usable_w - 10.0f) / 2.0f; // 251 px
    float card_audio_y = 284.0f;
    float card_audio_h = 54.0f;

    // 1. System Audio Card
    g_rects.btnAudio = D2D1::RectF(24, card_audio_y, 24 + card_audio_w, card_audio_y + card_audio_h);
    D2D1_ROUNDED_RECT rcAudio = D2D1::RoundedRect(g_rects.btnAudio, 8, 8);
    g_rt->FillRoundedRectangle(rcAudio, bCard);
    g_rt->DrawRoundedRectangle(rcAudio, g_cfg.audio_enabled ? bAccent : bBorder, 1.0f);

    D2D1_RECT_F rAudTitle = D2D1::RectF(34, card_audio_y + 8, 24 + card_audio_w - 10, card_audio_y + 26);
    std::wstring wsAudTitle = g_cfg.audio_enabled ? L"🔊 SYSTEM AUDIO: ON" : L"🔇 SYSTEM AUDIO: MUTED";
    d2d_draw(g_rt, wsAudTitle, g_tf_bold, rAudTitle, g_cfg.audio_enabled ? bGreen : bMuted);

    D2D1_RECT_F rAudDesc = D2D1::RectF(34, card_audio_y + 28, 24 + card_audio_w - 10, card_audio_y + 46);
    d2d_draw(g_rt, L"Captures games & PC audio", g_tf_small, rAudDesc, bMuted);

    // 2. Discord Isolation Card
    DWORD discord_pid = find_discord_root_pid();
    g_rects.btnDiscord = D2D1::RectF(24 + card_audio_w + 10.0f, card_audio_y, WINDOW_WIDTH - 24, card_audio_y + card_audio_h);
    D2D1_ROUNDED_RECT rcDiscord = D2D1::RoundedRect(g_rects.btnDiscord, 8, 8);
    g_rt->FillRoundedRectangle(rcDiscord, bCard);
    g_rt->DrawRoundedRectangle(rcDiscord, g_cfg.exclude_discord ? bAccent : bBorder, 1.0f);

    D2D1_RECT_F rDiscTitle = D2D1::RectF(24 + card_audio_w + 20.0f, card_audio_y + 8, WINDOW_WIDTH - 30, card_audio_y + 26);
    std::wstring wsDiscTitle = g_cfg.exclude_discord ? L"🎧 EXCLUDE DISCORD: ON" : L"🎧 EXCLUDE DISCORD: OFF";
    d2d_draw(g_rt, wsDiscTitle, g_tf_bold, rDiscTitle, g_cfg.exclude_discord ? bAccent : bMuted);

    D2D1_RECT_F rDiscDesc = D2D1::RectF(24 + card_audio_w + 20.0f, card_audio_y + 28, WINDOW_WIDTH - 30, card_audio_y + 46);
    if (g_cfg.exclude_discord) {
        if (discord_pid != 0) {
            d2d_draw(g_rt, L"● Discord Muted (Friends hear no echo)", g_tf_small, rDiscDesc, bGreen);
        } else {
            d2d_draw(g_rt, L"○ Waiting for Discord to open...", g_tf_small, rDiscDesc, bMuted);
        }
    } else {
        d2d_draw(g_rt, L"All PC sound broadcasted (No filter)", g_tf_small, rDiscDesc, bMuted);
    }

    // -------------------------------------------------------------
    // SECTION 4: VIEWER LINK CARD
    // -------------------------------------------------------------
    D2D1_ROUNDED_RECT rcLink = D2D1::RoundedRect(D2D1::RectF(24, 350, WINDOW_WIDTH - 24, 434), 8, 8);
    g_rt->FillRoundedRectangle(rcLink, bCard);
    g_rt->DrawRoundedRectangle(rcLink, bBorder, 1.0f);

    D2D1_RECT_F rLinkLbl = D2D1::RectF(36, 358, 480, 374);
    d2d_draw(g_rt, L"VIEWER LINK (ส่งให้เพื่อนดูผ่าน Browser ได้ทันที):", g_tf_section_lbl, rLinkLbl, bMuted);

    std::string view_url = g_cfg.server_url;
    while (!view_url.empty() && view_url.back() == '/') view_url.pop_back();
    view_url += "/r/" + g_cfg.room_id;
    std::wstring wsViewUrl = to_wide(view_url);

    D2D1_RECT_F rLinkVal = D2D1::RectF(36, 376, WINDOW_WIDTH - 36, 396);
    d2d_draw(g_rt, wsViewUrl, g_tf_mono, rLinkVal, bAccent);

    // 3 Buttons: COPY LINK, NEW ROOM, OPEN WEB
    g_rects.btnCopy = D2D1::RectF(36, 400, 196, 426);
    D2D1_ROUNDED_RECT rcCopy = D2D1::RoundedRect(g_rects.btnCopy, 4, 4);
    g_rt->FillRoundedRectangle(rcCopy, g_copied_feedback ? bGreen : bAccent);
    if (g_copied_feedback) {
        d2d_draw(g_rt, L"COPIED!", g_tf_btn_center, g_rects.btnCopy, bText);
    } else {
        d2d_draw(g_rt, L"📋 COPY LINK", g_tf_btn_center, g_rects.btnCopy, bText);
    }

    g_rects.btnNewRoom = D2D1::RectF(204, 400, 344, 426);
    D2D1_ROUNDED_RECT rcNewRoom = D2D1::RoundedRect(g_rects.btnNewRoom, 4, 4);
    g_rt->FillRoundedRectangle(rcNewRoom, bBorder);
    d2d_draw(g_rt, L"🔄 NEW ROOM", g_tf_btn_center, g_rects.btnNewRoom, bText);

    g_rects.btnOpenWeb = D2D1::RectF(352, 400, 472, 426);
    D2D1_ROUNDED_RECT rcOpenWeb = D2D1::RoundedRect(g_rects.btnOpenWeb, 4, 4);
    g_rt->FillRoundedRectangle(rcOpenWeb, bBorder);
    d2d_draw(g_rt, L"🌐 OPEN WEB", g_tf_btn_center, g_rects.btnOpenWeb, bText);

    // -------------------------------------------------------------
    // SECTION 5: BIG ACTION BUTTON (START / STOP SHARING)
    // -------------------------------------------------------------
    g_rects.btnStartStop = D2D1::RectF(24, 446, WINDOW_WIDTH - 24, 520);
    D2D1_ROUNDED_RECT rcAction = D2D1::RoundedRect(g_rects.btnStartStop, 12, 12);
    if (g_state.is_streaming) {
        g_rt->FillRoundedRectangle(rcAction, bRed);
        d2d_draw(g_rt, L"\u25A0  STOP SHARING SCREEN", g_tf_title_center, g_rects.btnStartStop, bText);
    } else {
        g_rt->FillRoundedRectangle(rcAction, bAccent);
        d2d_draw(g_rt, L"\u25CF  START SHARING SCREEN", g_tf_title_center, g_rects.btnStartStop, bText);
    }

    // -------------------------------------------------------------
    // SECTION 6: STATUS BANNER & METRICS
    // -------------------------------------------------------------
    D2D1_ROUNDED_RECT rcStatus = D2D1::RoundedRect(D2D1::RectF(24, 532, WINDOW_WIDTH - 24, 614), 8, 8);
    g_rt->FillRoundedRectangle(rcStatus, bCard);
    g_rt->DrawRoundedRectangle(rcStatus, bBorder, 1.0f);

    std::wstring wsStatus = to_wide(g_state.status_text);
    D2D1_RECT_F rStatTxt = D2D1::RectF(36, 544, WINDOW_WIDTH - 36, 568);
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
        D2D1_RECT_F rTimeTxt = D2D1::RectF(36, 572, WINDOW_WIDTH - 36, 598);
        d2d_draw(g_rt, time_buf, g_tf_small, rTimeTxt, bMuted);
    } else {
        D2D1_RECT_F rSubTxt = D2D1::RectF(36, 572, WINDOW_WIDTH - 36, 598);
        d2d_draw(g_rt, L"Press Start Sharing to stream instantly to friends via low-latency WebRTC.", g_tf_small, rSubTxt, bMuted);
    }

    // Release brushes
    if (bCard) bCard->Release();
    if (bText) bText->Release();
    if (bMuted) bMuted->Release();
    if (bAccent) bAccent->Release();
    if (bAccentBg) bAccentBg->Release();
    if (bRed) bRed->Release();
    if (bGreen) bGreen->Release();
    if (bBorder) bBorder->Release();
    if (bBg) bBg->Release();

    g_rt->EndDraw();
}

static bool hit_test(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        if (pt.y < 60 && !hit_test(g_rects.btnClose, pt.x, pt.y) && !hit_test(g_rects.btnMinimize, pt.x, pt.y)) {
            return HTCAPTION; // Allow dragging the borderless window
        }
        return HTCLIENT;
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

    // Parse any server URL passed on command line
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

    // Create Job Object to kill child processes on exit
    g_job = CreateJobObjectW(nullptr, nullptr);
    if (g_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2d);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw);

    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"en-US", &g_tf_title);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"en-US", &g_tf_title_center);
    g_tf_title_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_title_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-US", &g_tf_bold);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-US", &g_tf_regular);
    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"en-US", &g_tf_small);
    g_dw->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-US", &g_tf_mono);

    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-US", &g_tf_btn_center);
    g_tf_btn_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_btn_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-US", &g_tf_ctrl_center);
    g_tf_ctrl_center->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_tf_ctrl_center->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    g_dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.5f, L"en-US", &g_tf_section_lbl);

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

    // Borderless window with drop shadow
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

    SetTimer(g_hwnd, 1, 500, nullptr); // Update live timer every 500ms

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
    if (g_rt) g_rt->Release();
    if (g_d2d) g_d2d->Release();
    if (g_dw) g_dw->Release();
    if (g_job) CloseHandle(g_job);

    return 0;
}
