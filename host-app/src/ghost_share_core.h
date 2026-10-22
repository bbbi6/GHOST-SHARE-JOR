#pragma once
#include <string>
#include <random>
#include <vector>
#include <unordered_set>
#include <chrono>
#include <windows.h>
#include <tlhelp32.h>

enum class ScreenResolution {
    Source = 0,   // Native / Original Display
    Res1440p = 1, // 2560x1440 QHD
    Res1080p = 2, // 1920x1080 FHD
    Res720p = 3   // 1280x720 HD
};

enum class ScreenFps {
    Fps60 = 60,
    Fps30 = 30
};

struct ShareConfig {
    std::string server_url = "http://localhost:8889";
    std::string room_id;
    ScreenResolution resolution = ScreenResolution::Res1080p; // 1080p default
    ScreenFps fps = ScreenFps::Fps60;                        // 60 FPS default
    int bitrate_mbps = 16;                                   // 16 Mbps default
    bool audio_enabled = true;
    bool exclude_discord = true;                             // True by default: isolate Discord!
    bool use_nvenc = true;
};

struct ShareState {
    bool is_streaming = false;
    uint32_t start_time = 0;
    int viewer_count = 0;
    std::string status_text = "Ready to share";
    std::string error_msg;
};

inline std::string generate_random_room() {
    static const char chars[] = "abcdefghjkmnpqrstuvwxyz23456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(chars) - 2);
    std::string room = "ghost_";
    for (int i = 0; i < 6; ++i) {
        room += chars[dis(gen)];
    }
    return room;
}

inline const wchar_t* resolution_title(ScreenResolution r) {
    switch (r) {
        case ScreenResolution::Source:   return L"Source (Native)";
        case ScreenResolution::Res1440p: return L"1440p (QHD)";
        case ScreenResolution::Res1080p: return L"1080p (FHD)";
        case ScreenResolution::Res720p:  return L"720p (HD)";
    }
    return L"1080p (FHD)";
}

inline const wchar_t* fps_title(ScreenFps f) {
    return (f == ScreenFps::Fps30) ? L"30 FPS" : L"60 FPS";
}

inline DWORD find_discord_root_pid() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = { sizeof(pe) };
    std::vector<PROCESSENTRY32W> discord_procs;
    std::unordered_set<DWORD> discord_pids;

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"Discord.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"DiscordCanary.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"DiscordPTB.exe") == 0) {
                discord_procs.push_back(pe);
                discord_pids.insert(pe.th32ProcessID);
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);

    if (discord_procs.empty()) return 0;

    // Find the Discord process whose parent is NOT another Discord process
    for (const auto& proc : discord_procs) {
        if (discord_pids.find(proc.th32ParentProcessID) == discord_pids.end()) {
            return proc.th32ProcessID;
        }
    }

    return discord_procs[0].th32ProcessID;
}

inline std::string build_whip_pipeline(const ShareConfig& cfg) {
    std::string fps_str = (cfg.fps == ScreenFps::Fps30) ? "30" : "60";
    
    int mbps = cfg.bitrate_mbps;
    if (mbps < 2) mbps = 2;
    if (mbps > 80) mbps = 80;
    std::string max_bitrate_bps = std::to_string(mbps * 1000000);

    // Clean endpoint
    std::string base_url = cfg.server_url;
    while (!base_url.empty() && (base_url.back() == '/' || base_url.back() == ' ')) {
        base_url.pop_back();
    }
    while (!base_url.empty() && base_url.front() == ' ') {
        base_url.erase(base_url.begin());
    }
    if (base_url.rfind("http://", 0) != 0 && base_url.rfind("https://", 0) != 0) {
        base_url = "https://" + base_url;
    }
    std::string whip_endpoint = base_url + "/" + cfg.room_id + "/whip";

    std::string scale_caps = "";
    if (cfg.resolution == ScreenResolution::Res1440p) {
        scale_caps = ",width=2560,height=1440";
    } else if (cfg.resolution == ScreenResolution::Res1080p) {
        scale_caps = ",width=1920,height=1080";
    } else if (cfg.resolution == ScreenResolution::Res720p) {
        scale_caps = ",width=1280,height=720";
    }

    std::string video_filter = " ! d3d11convert ! video/x-raw(memory:D3D11Memory)" + scale_caps + ",format=NV12 ! d3d11download ! video/x-raw,format=NV12 ";

    std::string pipeline = 
        "d3d11screencapturesrc do-timestamp=true show-cursor=true ! video/x-raw(memory:D3D11Memory),framerate=" + fps_str + "/1 " +
        video_filter + "! queue ! whip. ";

    if (cfg.audio_enabled) {
        DWORD discord_pid = 0;
        if (cfg.exclude_discord) {
            discord_pid = find_discord_root_pid();
        }

        if (cfg.exclude_discord && discord_pid != 0) {
            pipeline += "wasapi2src loopback=true loopback-mode=exclude-process-tree loopback-target-pid=" + std::to_string(discord_pid) + " ! audio/x-raw ! audioconvert ! audioresample ! queue ! whip. ";
        } else {
            pipeline += "wasapi2src loopback=true ! audio/x-raw ! audioconvert ! audioresample ! queue ! whip. ";
        }
    }

    pipeline += "whipclientsink name=whip max-bitrate=" + max_bitrate_bps + " video-caps=\"video/x-h264\" signaller::whip-endpoint=\"" + whip_endpoint + "\"";

    return pipeline;
}

inline void copy_text_to_clipboard(HWND hwnd, const std::string& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
    if (hGlob) {
        memcpy(GlobalLock(hGlob), text.c_str(), text.size() + 1);
        GlobalUnlock(hGlob);
        SetClipboardData(CF_TEXT, hGlob);
    }
    CloseClipboard();
}
