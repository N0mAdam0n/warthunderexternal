#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <sstream> 
#include <fstream>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <atomic>

#include "structs.hpp"
#include "offsets.hpp"
#include "memory.hpp"
#include "config.hpp"
#include "input.hpp"
#include "ui.hpp"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include <d3d11.h>

#pragma comment(lib, "d3d11.lib")


Memory mem;
int ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
int ScreenHeight = GetSystemMetrics(SM_CYSCREEN);
HWND g_hwndOverlay = NULL;
bool bShowMenu = true;
static HANDLE g_instanceMutex = NULL;
ImFont* g_espFont = nullptr;

namespace app {
    std::atomic<bool> running{ true };
}

namespace shared {
    std::vector<CachedEntity> Entities;
    std::vector<CachedRocket> Rockets;
    Matrix4x4 ViewMatrix;
    Matrix4x4 ViewMatrixAlt;
    Vector3 LocalPos;
    Vector3 LocalUnitPos;
    Vector3 CCIPPos;
    float LiveVelocity = 800.0f;
    int LocalTeam;
    std::mutex DataMutex;
    std::atomic<uintptr_t> TargetHijackPtr(0);
    std::atomic<uint64_t> viewGeneration(0);
    std::atomic<uint64_t> entityGeneration(0);
    std::atomic<uint64_t> entityCacheTick(0);
    std::atomic<uint32_t> cachedEntityCount(0);
    std::atomic<uint32_t> rawUnitCount(0);
    std::atomic<bool> gameLinkOk(false);
}

namespace perf {
    std::atomic<uint32_t> viewFps(0);
    std::atomic<uint32_t> entityFps(0);
    std::atomic<uint32_t> drawFps(0);
    std::atomic<uint32_t> loopFps(0);
    std::atomic<uint32_t> cacheMs(0);
}

static void UpdatePerfCounters(uint64_t lastViewGen, uint64_t lastEntityGen, uint64_t& effectiveDrawFrames, uint64_t& loopFrames) {
    static uint64_t lastPerfTick = GetTickCount64();
    static uint64_t lastViewCount = 0;
    static uint64_t lastEntityCount = 0;
    static uint64_t lastLoopCount = 0;
    static uint64_t lastDrawCount = 0;

    loopFrames++;

    const uint64_t viewGen = shared::viewGeneration.load(std::memory_order_relaxed);
    const uint64_t entityGen = shared::entityGeneration.load(std::memory_order_relaxed);
    if (viewGen != lastViewGen || entityGen != lastEntityGen) {
        effectiveDrawFrames++;
    }

    const uint64_t now = GetTickCount64();
    if (now - lastPerfTick < 1000) return;

    const uint64_t viewNow = viewGen;
    const uint64_t entityNow = entityGen;

    perf::viewFps.store(static_cast<uint32_t>(viewNow - lastViewCount), std::memory_order_relaxed);
    perf::entityFps.store(static_cast<uint32_t>(entityNow - lastEntityCount), std::memory_order_relaxed);
    perf::drawFps.store(static_cast<uint32_t>(effectiveDrawFrames - lastDrawCount), std::memory_order_relaxed);
    perf::loopFps.store(static_cast<uint32_t>(loopFrames - lastLoopCount), std::memory_order_relaxed);

    lastViewCount = viewNow;
    lastEntityCount = entityNow;
    lastDrawCount = effectiveDrawFrames;
    lastLoopCount = loopFrames;
    lastPerfTick = now;
}

namespace settings {
    bool bEulaAccepted = false;
    bool bStreamerMode = false;

    bool bMemoryAim = false;
    int aimKey = VK_RBUTTON;
    float aimFov = 150.0f;
    float aimSmooth = 4.0f;
    bool bShowFovCircle = true;  // now exposed as standalone toggle in 视觉 and 战斗 tabs
    bool bPrediction = false;
    bool bBulletDrop = false;
    float gravityScale = 0.88f;
    float targetHeightRatio = 0.5f;

    bool bEsp = true;
    bool bEspBots = true;
    bool bEspTeammates = false;
    bool bEspGround = true;
    bool bForceChineseNames = false;
    int espFontIndex = 0;
    float espFontSize = 18.0f;
    int espMarkerStyle = 0;  // 0: small dot, 1: solid circle, 2: hollow circle, 3: diamond, 4: square
    bool bBox = true;
    bool bBox3D = false;
    bool bFilledBox = true;
    bool bLines = false;
    bool bName = true;
    bool bDistance = true;
    bool bReloadBar = true;
    bool bFacing = true;
    bool bRadar = true;
    bool bMissileESP = true;

    bool bInternalsESP = false;
    int iInternalsMode = 0;
    bool bInternalsName = true;

    bool bCCIP = false;
    bool bAirLead = false;
    bool bDangerWarning = false;

    bool bAutoTeam = true;
    int ManualTeam = 1;

    bool bEnableMemoryWrites = false;
    bool bEnableEntityHijack = false;

    bool bForceArcadeCrosshair = false;
    bool bForceAirLead = false;
    bool bForceTankESP = false;

    bool bForceThermals = false;
    bool bMidAirReload = false;
    bool bGhostCollision = false;
    bool bSpamScout = false;
    bool bThrustMult = false;

    std::atomic<bool> bMindControlActive(false);

    float col_BoxVis[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    float col_BoxTeam[4] = { 0.2f, 0.85f, 0.35f, 1.0f };
    float col_Fov[4] = { 1.0f, 1.0f, 1.0f, 0.5f };
    float col_ESPText[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
}

extern void CacheThread();
extern void FastViewThread();
extern void RenderESP(ImDrawList* draw);

struct Notification { std::string message; float timer; float alpha; };
std::vector<Notification> g_Notifications;

void PushNotification(const std::string& msg) { g_Notifications.push_back({ msg, 4.0f, 0.0f }); }

void RenderNotifications(ImDrawList* draw) {
    if (g_Notifications.empty()) return;
    float startY = (float)ScreenHeight - 50.0f;
    float dt = ImGui::GetIO().DeltaTime;
    for (auto it = g_Notifications.begin(); it != g_Notifications.end();) {
        it->timer -= dt;
        if (it->timer > 3.5f) it->alpha += dt * 3.0f; else if (it->timer < 0.5f) it->alpha -= dt * 3.0f; else it->alpha = 1.0f;
        if (it->alpha < 0.0f) it->alpha = 0.0f; if (it->alpha > 1.0f) it->alpha = 1.0f;

        if (it->alpha > 0.01f) {
            ImVec2 textSize = ImGui::CalcTextSize(it->message.c_str());
            ImVec2 padding = ImVec2(15, 10);
            ImVec2 boxSize = ImVec2(textSize.x + padding.x * 2, textSize.y + padding.y * 2);
            ImVec2 pos = ImVec2((float)ScreenWidth / 2.0f - boxSize.x / 2.0f, startY);
            draw->AddRectFilled(pos, ImVec2(pos.x + boxSize.x, pos.y + boxSize.y), ImColor(17, 17, 21, (int)(255 * it->alpha)), 6.0f);
            draw->AddRectFilled(ImVec2(pos.x, pos.y + boxSize.y - 2), ImVec2(pos.x + boxSize.x, pos.y + boxSize.y), ImColor(219, 44, 44, (int)(255 * it->alpha)), 6.0f, ImDrawFlags_RoundCornersBottom);
            ImU32 notifCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings::col_ESPText[0], settings::col_ESPText[1], settings::col_ESPText[2], (float)(it->alpha * settings::col_ESPText[3])));
            draw->AddText(ImVec2(pos.x + padding.x, pos.y + padding.y), notifCol, it->message.c_str());
            startY -= (boxSize.y + 10.0f);
        }
        if (it->timer <= 0.0f) it = g_Notifications.erase(it); else ++it;
    }
}

void DrawWatermark(ImDrawList* draw) {
    // Cache time string; updating every frame is wasteful and hits CRT every tick
    static char timeBuf[80] = "00:00:00";
    static uint64_t lastTimeUpdate = 0;
    const uint64_t nowTs = GetTickCount64();
    if (nowTs - lastTimeUpdate >= 1000) {
        time_t raw; struct tm info;
        time(&raw); localtime_s(&info, &raw);
        strftime(timeBuf, sizeof(timeBuf), "%H:%M:%S", &info);
        lastTimeUpdate = nowTs;
    }

    int displayTeam = -1;
    const char* teamMode = "A";
    {
        std::lock_guard<std::mutex> lock(shared::DataMutex);
        if (settings::bAutoTeam) {
            displayTeam = shared::LocalTeam;
            teamMode = "A";
        }
        else {
            displayTeam = settings::ManualTeam;
            teamMode = "M";
        }
    }

    char text[320];
    _snprintf_s(text, _TRUNCATE, "JANG DMA (WT) | Draw:%u View:%u Data:%u | Ent:%u/%u | cGame:%s Team:%d(%s) | %ums | %s",
        perf::drawFps.load(), perf::viewFps.load(), perf::entityFps.load(),
        shared::cachedEntityCount.load(), shared::rawUnitCount.load(),
        shared::gameLinkOk.load(std::memory_order_relaxed) ? "OK" : "NO",
        displayTeam, teamMode, perf::cacheMs.load(), timeBuf);
    ImVec2 pos(20, 20); ImVec2 tSize = ImGui::CalcTextSize(text); ImVec2 pad(12, 6);
    ImVec2 bSize(tSize.x + pad.x * 2, tSize.y + pad.y * 2);
    draw->AddRectFilled(pos, ImVec2(pos.x + bSize.x, pos.y + bSize.y), ImColor(15, 15, 15, 200), 4.0f);
    draw->AddRectFilled(pos, ImVec2(pos.x + bSize.x, pos.y + 2), ImColor(219, 44, 44, 255), 4.0f, ImDrawFlags_RoundCornersTop);
    ImU32 wmCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings::col_ESPText[0], settings::col_ESPText[1], settings::col_ESPText[2], settings::col_ESPText[3]));
    draw->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), wmCol, text);
}

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            ID3D11Texture2D* pBackBuffer = nullptr; HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)); if (SUCCEEDED(hr) && pBackBuffer) { g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView); pBackBuffer->Release(); }
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void ResizeOverlayRenderTargets(int width, int height) {
    if (!g_pSwapChain || width <= 0 || height <= 0) return;

    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }

    g_pSwapChain->ResizeBuffers(0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (SUCCEEDED(hr) && pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f; style.ChildRounding = 6.0f; style.FrameRounding = 4.0f; style.GrabRounding = 4.0f; style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f; style.TabRounding = 4.0f; style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
}

void LoadESPFont() {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    ImFontConfig config; config.OversampleH = 2; config.OversampleV = 2;
    const ImWchar* glyphRanges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();

    // Always load a reliable Chinese font first for the menu/UI (stable Chinese support)
    const char* menuFontPath = "C:\\Windows\\Fonts\\msyh.ttc";
    ImFont* menuFont = nullptr;
    if (std::ifstream(menuFontPath).good()) {
        menuFont = io.Fonts->AddFontFromFileTTF(menuFontPath, 18.0f, &config, glyphRanges);
    } else {
        menuFont = io.Fonts->AddFontDefault();
    }

    // Load separate font for ESP drawing only (user selectable, can be English or Chinese)
    const char* fontPaths[] = {
        // Chinese fonts
        "C:\\Windows\\Fonts\\msyh.ttc",   // 微软雅黑
        "C:\\Windows\\Fonts\\simhei.ttf", // 黑体
        "C:\\Windows\\Fonts\\simsun.ttc", // 宋体
        // English fonts (good compatibility and readability)
        "C:\\Windows\\Fonts\\consola.ttf", // Consolas (monospace, excellent for numbers/distances)
        "C:\\Windows\\Fonts\\arial.ttf",   // Arial (clean sans-serif)
        "C:\\Windows\\Fonts\\segoeui.ttf", // Segoe UI (modern Windows default)
        "C:\\Windows\\Fonts\\verdana.ttf", // Verdana (highly legible)
        "C:\\Windows\\Fonts\\calibri.ttf", // Calibri (smooth, professional)
        "C:\\Windows\\Fonts\\tahoma.ttf",  // Tahoma
        "C:\\Windows\\Fonts\\cour.ttf",    // Courier New (monospace classic)
        nullptr                            // fallback to menu font
    };
    int idx = settings::espFontIndex;
    ImFont* espFont = nullptr;
    if (idx >= 0 && idx < (int)(sizeof(fontPaths)/sizeof(fontPaths[0])) - 1 && fontPaths[idx] && std::ifstream(fontPaths[idx]).good()) {
        espFont = io.Fonts->AddFontFromFileTTF(fontPaths[idx], settings::espFontSize, &config, glyphRanges);
    }
    if (!espFont) {
        espFont = menuFont;
    }
    g_espFont = espFont;

    io.Fonts->Build();

    // Ensure DX11 font texture is updated for runtime font changes
    ImGui_ImplDX11_InvalidateDeviceObjects();
    ImGui_ImplDX11_CreateDeviceObjects();
}

bool g_fontReloadNeeded = true; // initial load

static bool EnsureOverlayDimensions() {
    if (ScreenWidth < 100 || ScreenHeight < 100) {
        ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
        ScreenHeight = GetSystemMetrics(SM_CYSCREEN);
    }
    return ScreenWidth >= 100 && ScreenHeight >= 100;
}

static std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
    return value.substr(start, end - start);
}

static void ApplyUserConfigValue(const std::string& section, const std::string& key, const std::string& rawValue) {
    std::string value = Trim(rawValue);

    if (section == "aim") {
        if (key == "memory_aim") settings::bMemoryAim = (value == "1" || value == "true" || value == "yes");
        else if (key == "aim_key" && !value.empty()) settings::aimKey = std::stoi(value);
        else if (key == "aim_fov" && !value.empty()) settings::aimFov = std::stof(value);
        else if (key == "aim_smooth" && !value.empty()) settings::aimSmooth = std::stof(value);
        else if (key == "show_fov_circle") settings::bShowFovCircle = (value == "1" || value == "true" || value == "yes");
        else if (key == "prediction") settings::bPrediction = (value == "1" || value == "true" || value == "yes");
        else if (key == "bullet_drop") settings::bBulletDrop = (value == "1" || value == "true" || value == "yes");
        else if (key == "gravity_scale" && !value.empty()) settings::gravityScale = std::stof(value);
        else if (key == "target_height_ratio" && !value.empty()) settings::targetHeightRatio = std::stof(value);
    } else if (section == "esp") {
        if (key == "esp") settings::bEsp = (value == "1" || value == "true" || value == "yes");
        else if (key == "esp_bots") settings::bEspBots = (value == "1" || value == "true" || value == "yes");
        else if (key == "esp_teammates") settings::bEspTeammates = (value == "1" || value == "true" || value == "yes");
        else if (key == "esp_ground") settings::bEspGround = (value == "1" || value == "true" || value == "yes");
        else if (key == "force_chinese_names") settings::bForceChineseNames = (value == "1" || value == "true" || value == "yes");
        else if (key == "esp_font_index" && !value.empty()) settings::espFontIndex = std::stoi(value);
        else if (key == "esp_font_size" && !value.empty()) settings::espFontSize = std::stof(value);
        else if (key == "esp_marker_style" && !value.empty()) settings::espMarkerStyle = std::stoi(value);
        else if (key == "box") settings::bBox = (value == "1" || value == "true" || value == "yes");
        else if (key == "box_3d") settings::bBox3D = (value == "1" || value == "true" || value == "yes");
        else if (key == "filled_box") settings::bFilledBox = (value == "1" || value == "true" || value == "yes");
        else if (key == "lines") settings::bLines = (value == "1" || value == "true" || value == "yes");
        else if (key == "name") settings::bName = (value == "1" || value == "true" || value == "yes");
        else if (key == "distance") settings::bDistance = (value == "1" || value == "true" || value == "yes");
        else if (key == "reload_bar") settings::bReloadBar = (value == "1" || value == "true" || value == "yes");
        else if (key == "facing") settings::bFacing = (value == "1" || value == "true" || value == "yes");
        else if (key == "radar") settings::bRadar = (value == "1" || value == "true" || value == "yes");
        else if (key == "missile_esp") settings::bMissileESP = (value == "1" || value == "true" || value == "yes");
        else if (key == "internals_esp") settings::bInternalsESP = (value == "1" || value == "true" || value == "yes");
        else if (key == "internals_mode" && !value.empty()) settings::iInternalsMode = std::stoi(value);
        else if (key == "internals_name") settings::bInternalsName = (value == "1" || value == "true" || value == "yes");
    } else if (section == "features") {
        if (key == "ccip") settings::bCCIP = (value == "1" || value == "true" || value == "yes");
        else if (key == "air_lead") settings::bAirLead = (value == "1" || value == "true" || value == "yes");
        else if (key == "danger_warning") settings::bDangerWarning = (value == "1" || value == "true" || value == "yes");
        else if (key == "enable_memory_writes") settings::bEnableMemoryWrites = (value == "1" || value == "true" || value == "yes");
        else if (key == "enable_entity_hijack") settings::bEnableEntityHijack = (value == "1" || value == "true" || value == "yes");
        else if (key == "force_arcade_crosshair") settings::bForceArcadeCrosshair = (value == "1" || value == "true" || value == "yes");
        else if (key == "force_air_lead") settings::bForceAirLead = (value == "1" || value == "true" || value == "yes");
        else if (key == "force_tank_esp") settings::bForceTankESP = (value == "1" || value == "true" || value == "yes");
        else if (key == "force_thermals") settings::bForceThermals = (value == "1" || value == "true" || value == "yes");
        else if (key == "mid_air_reload") settings::bMidAirReload = (value == "1" || value == "true" || value == "yes");
        else if (key == "ghost_collision") settings::bGhostCollision = (value == "1" || value == "true" || value == "yes");
        else if (key == "spam_scout") settings::bSpamScout = (value == "1" || value == "true" || value == "yes");
        else if (key == "thrust_mult") settings::bThrustMult = (value == "1" || value == "true" || value == "yes");
    } else if (section == "general") {
        if (key == "auto_team") settings::bAutoTeam = (value == "1" || value == "true" || value == "yes");
        else if (key == "manual_team" && !value.empty()) settings::ManualTeam = std::stoi(value);
        else if (key == "streamer_mode") settings::bStreamerMode = (value == "1" || value == "true" || value == "yes");
    } else if (section == "colors") {
        auto parseColor = [](const std::string& v, float* col) {
            std::istringstream iss(v);
            std::string token;
            int i = 0;
            while (std::getline(iss, token, ',') && i < 4) {
                col[i++] = std::stof(Trim(token));
            }
        };
        if (key == "col_box_vis") parseColor(value, settings::col_BoxVis);
        else if (key == "col_box_team") parseColor(value, settings::col_BoxTeam);
        else if (key == "col_fov") parseColor(value, settings::col_Fov);
        else if (key == "col_esp_text") parseColor(value, settings::col_ESPText);
    }
}

bool LoadUserSettings(const char* path = "settings.ini") {
    std::ifstream file(path);
    if (!file.good()) return false;

    std::string line;
    std::string section;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));
        ApplyUserConfigValue(section, key, value);
    }

    return true;
}

void SaveUserSettings(const char* path = "settings.ini") {
    std::ofstream f(path);
    if (!f.good()) return;

    f << "# JANG WT DMA - User settings (auto-saved on exit)\n\n";

    f << "[aim]\n";
    f << "memory_aim=" << (settings::bMemoryAim ? 1 : 0) << "\n";
    f << "aim_key=" << settings::aimKey << "\n";
    f << "aim_fov=" << settings::aimFov << "\n";
    f << "aim_smooth=" << settings::aimSmooth << "\n";
    f << "show_fov_circle=" << (settings::bShowFovCircle ? 1 : 0) << "\n";
    f << "prediction=" << (settings::bPrediction ? 1 : 0) << "\n";
    f << "bullet_drop=" << (settings::bBulletDrop ? 1 : 0) << "\n";
    f << "gravity_scale=" << settings::gravityScale << "\n";
    f << "target_height_ratio=" << settings::targetHeightRatio << "\n\n";

    f << "[esp]\n";
    f << "esp=" << (settings::bEsp ? 1 : 0) << "\n";
    f << "esp_bots=" << (settings::bEspBots ? 1 : 0) << "\n";
    f << "esp_teammates=" << (settings::bEspTeammates ? 1 : 0) << "\n";
    f << "esp_ground=" << (settings::bEspGround ? 1 : 0) << "\n";
    f << "force_chinese_names=" << (settings::bForceChineseNames ? 1 : 0) << "\n";
    f << "esp_font_index=" << settings::espFontIndex << "\n";
    f << "esp_font_size=" << settings::espFontSize << "\n";
    f << "esp_marker_style=" << settings::espMarkerStyle << "\n";
    f << "box=" << (settings::bBox ? 1 : 0) << "\n";
    f << "box_3d=" << (settings::bBox3D ? 1 : 0) << "\n";
    f << "filled_box=" << (settings::bFilledBox ? 1 : 0) << "\n";
    f << "lines=" << (settings::bLines ? 1 : 0) << "\n";
    f << "name=" << (settings::bName ? 1 : 0) << "\n";
    f << "distance=" << (settings::bDistance ? 1 : 0) << "\n";
    f << "reload_bar=" << (settings::bReloadBar ? 1 : 0) << "\n";
    f << "facing=" << (settings::bFacing ? 1 : 0) << "\n";
    f << "radar=" << (settings::bRadar ? 1 : 0) << "\n";
    f << "missile_esp=" << (settings::bMissileESP ? 1 : 0) << "\n";
    f << "internals_esp=" << (settings::bInternalsESP ? 1 : 0) << "\n";
    f << "internals_mode=" << settings::iInternalsMode << "\n";
    f << "internals_name=" << (settings::bInternalsName ? 1 : 0) << "\n\n";

    f << "[features]\n";
    f << "ccip=" << (settings::bCCIP ? 1 : 0) << "\n";
    f << "air_lead=" << (settings::bAirLead ? 1 : 0) << "\n";
    f << "danger_warning=" << (settings::bDangerWarning ? 1 : 0) << "\n";
    f << "enable_memory_writes=" << (settings::bEnableMemoryWrites ? 1 : 0) << "\n";
    f << "enable_entity_hijack=" << (settings::bEnableEntityHijack ? 1 : 0) << "\n";
    f << "force_arcade_crosshair=" << (settings::bForceArcadeCrosshair ? 1 : 0) << "\n";
    f << "force_air_lead=" << (settings::bForceAirLead ? 1 : 0) << "\n";
    f << "force_tank_esp=" << (settings::bForceTankESP ? 1 : 0) << "\n";
    f << "force_thermals=" << (settings::bForceThermals ? 1 : 0) << "\n";
    f << "mid_air_reload=" << (settings::bMidAirReload ? 1 : 0) << "\n";
    f << "ghost_collision=" << (settings::bGhostCollision ? 1 : 0) << "\n";
    f << "spam_scout=" << (settings::bSpamScout ? 1 : 0) << "\n";
    f << "thrust_mult=" << (settings::bThrustMult ? 1 : 0) << "\n\n";

    f << "[general]\n";
    f << "auto_team=" << (settings::bAutoTeam ? 1 : 0) << "\n";
    f << "manual_team=" << settings::ManualTeam << "\n";
    f << "streamer_mode=" << (settings::bStreamerMode ? 1 : 0) << "\n\n";

    f << "[colors]\n";
    f << "col_box_vis=" << settings::col_BoxVis[0] << "," << settings::col_BoxVis[1] << "," << settings::col_BoxVis[2] << "," << settings::col_BoxVis[3] << "\n";
    f << "col_box_team=" << settings::col_BoxTeam[0] << "," << settings::col_BoxTeam[1] << "," << settings::col_BoxTeam[2] << "," << settings::col_BoxTeam[3] << "\n";
    f << "col_fov=" << settings::col_Fov[0] << "," << settings::col_Fov[1] << "," << settings::col_Fov[2] << "," << settings::col_Fov[3] << "\n";
    f << "col_esp_text=" << settings::col_ESPText[0] << "," << settings::col_ESPText[1] << "," << settings::col_ESPText[2] << "," << settings::col_ESPText[3] << "\n";
}

static void SetWorkingDirectoryToExe() {
    wchar_t modulePath[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) return;

    wchar_t* slash = wcsrchr(modulePath, L'\\');
    if (!slash) slash = wcsrchr(modulePath, L'/');
    if (!slash) return;

    *slash = L'\0';
    SetCurrentDirectoryW(modulePath);
}

static bool AcquireSingleInstance() {
    g_instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\JANG_WT_DMA_V1");
    if (!g_instanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(
            nullptr,
            L"JANG WT is already running.\n"
            L"Stop the previous debug session or close warthunderexternal.exe before starting again.",
            L"JANG WT",
            MB_ICONWARNING | MB_OK
        );
        if (g_instanceMutex) {
            CloseHandle(g_instanceMutex);
            g_instanceMutex = nullptr;
        }
        return false;
    }
    return true;
}

static void ShutdownApplication() {
    static std::atomic<bool> shutdownOnce{ false };
    if (shutdownOnce.exchange(true)) return;

    SaveUserSettings();
    std::cout << " [+] Saved settings.ini" << std::endl;

    app::running.store(false, std::memory_order_relaxed);
    Sleep(500);

    Input::Shutdown();
    mem.Disconnect();

    if (g_pd3dDeviceContext) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }

    if (g_hwndOverlay) {
        DestroyWindow(g_hwndOverlay);
        g_hwndOverlay = NULL;
    }

    if (g_instanceMutex) {
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
    }
}

bool InitOverlay() {
    mem.DetectGameResolution();
    mem.UpdateGameWindow();
    if (!EnsureOverlayDimensions()) {
        std::cout << "[!] Invalid ESP window size." << std::endl;
        return false;
    }

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"JangEspClass", NULL };
    RegisterClassExW(&wc);
    g_hwndOverlay = CreateWindowExW(
        WS_EX_TOPMOST,
        L"JangEspClass", L"JANG WT ESP", WS_POPUP,
        mem.LastRect.left, mem.LastRect.top, ScreenWidth, ScreenHeight,
        NULL, NULL, wc.hInstance, NULL);
    if (!g_hwndOverlay) {
        std::cout << "[!] Failed to create ESP window. Error=" << GetLastError() << std::endl;
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = ScreenWidth;
    sd.BufferDesc.Height = ScreenHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hwndOverlay;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, NULL, &g_pd3dDeviceContext);
    if (FAILED(hr) || !g_pSwapChain || !g_pd3dDevice || !g_pd3dDeviceContext) {
        std::cout << "[!] D3D11 overlay init failed. HRESULT=0x" << std::hex << hr << std::dec << std::endl;
        DestroyWindow(g_hwndOverlay);
        g_hwndOverlay = NULL;
        return false;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr) || !pBackBuffer) {
        std::cout << "[!] Failed to create overlay render target." << std::endl;
        return false;
    }
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwndOverlay);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    SetupStyle();
    LoadESPFont();
    g_fontReloadNeeded = false;
    SetWindowPos(g_hwndOverlay, HWND_TOPMOST, mem.LastRect.left, mem.LastRect.top, ScreenWidth, ScreenHeight, SWP_SHOWWINDOW);
    UpdateWindow(g_hwndOverlay);
    SetForegroundWindow(g_hwndOverlay);
    BringWindowToTop(g_hwndOverlay);
    return true;
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleTitleA("JANG DMA Loader [WT EDITION]");
    SetWorkingDirectoryToExe();
    if (!AcquireSingleInstance()) return 1;

    std::cout << "[>] Initializing DMA edition..." << std::endl;
    std::cout << " [i] Working directory: ";
    {
        wchar_t cwd[MAX_PATH]{};
        GetCurrentDirectoryW(MAX_PATH, cwd);
        std::wcout << cwd << std::endl;
    }

    std::ifstream f("license.dat"); if (f.good()) settings::bEulaAccepted = true;

    if (LoadDmaConfig()) {
        std::cout << " [+] Loaded dma_config.ini" << std::endl;
    }
    else {
        std::cout << "[!] dma_config.ini not found, using defaults." << std::endl;
    }

    if (LoadUserSettings()) {
        std::cout << " [+] Loaded settings.ini" << std::endl;
    }
    else {
        std::cout << "[!] settings.ini not found, using defaults." << std::endl;
    }

    std::cout << "[>] Connecting to DMA device..." << std::endl;
    if (!mem.Connect()) {
        std::cout << "[!] Failed to connect to DMA device." << std::endl;
        ShutdownApplication();
        return 1;
    }

    if (settings::bUseKmbox) {
        Input::Init();
    }

    std::cout << "[>] Waiting for aces.exe on target machine... (Press END to cancel)" << std::endl;
    while (mem.GetPID(L"aces.exe") == 0) {
        if (GetAsyncKeyState(VK_END) & 1) {
            ShutdownApplication();
            return 0;
        }
        Sleep(1000);
    }
    std::cout << " [+] Found aces.exe!" << std::endl;

    std::cout << " [>] Attaching to process..." << std::endl;
    while (!mem.Attach("aces.exe")) {
        if (GetAsyncKeyState(VK_END) & 1) {
            ShutdownApplication();
            return 0;
        }
        Sleep(1000);
    }
    std::cout << " [+] Attached successfully! Fetching offsets from API..." << std::endl;

    if (!mem.UpdateOffsets()) {
        std::cout << "[!] Using built-in offset defaults." << std::endl;
    }

    std::cout << "[>] Detecting game resolution..." << std::endl;
    mem.DetectGameResolution();
    std::cout << " [+] Game resolution: " << mem.GameScreenWidth << "x" << mem.GameScreenHeight
        << " (source=" << mem.resolutionSource << ")" << std::endl;

    if (!InitOverlay()) {
        std::cout << "[!] ESP window initialization failed." << std::endl;
        ShutdownApplication();
        return 1;
    }
    std::cout << " [+] ESP window ready: " << ScreenWidth << "x" << ScreenHeight
        << " @ (" << mem.LastRect.left << "," << mem.LastRect.top << ")" << std::endl;

    std::cout << " [+] Background data threads starting (enter a match for ESP)..." << std::endl;

    std::thread(FastViewThread).detach();
    std::thread(CacheThread).detach();

    float menuAlpha = 1.0f; int activeTab = 0;
    LONG exStyle = GetWindowLong(g_hwndOverlay, GWL_EXSTYLE);
    bool isClickThrough = !bShowMenu;
    uint64_t lastSeenViewGen = 0;
    uint64_t lastSeenEntityGen = 0;
    uint64_t effectiveDrawFrames = 0;
    uint64_t loopFrames = 0;

    if (bShowMenu) { SetWindowLong(g_hwndOverlay, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT); SetForegroundWindow(g_hwndOverlay); }
    else { SetWindowLong(g_hwndOverlay, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT); }

    while (app::running.load(std::memory_order_relaxed)) {
        MSG msg; while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                ShutdownApplication();
                return 0;
            }
        }
        if (GetAsyncKeyState(VK_END) & 1) break;

        // Throttle expensive window/DMA sync (was every frame) to keep the UI thread responsive
    // and prevent DMA reads from stalling the message pump / causing window freeze.
        {
            static uint64_t lastWinUpdate = 0;
            const uint64_t now = GetTickCount64();
            if (now - lastWinUpdate >= 120) {  // ~8 Hz is more than enough for position/resolution sync
                mem.UpdateGameWindow();
                lastWinUpdate = now;
            }
        }

        static auto lastToggle = GetTickCount64();
        if ((GetAsyncKeyState(VK_INSERT) & 1) && (GetTickCount64() - lastToggle > 200)) {
            bShowMenu = !bShowMenu;
            lastToggle = GetTickCount64();
            if (bShowMenu) {
                SetForegroundWindow(g_hwndOverlay);
            }
        }

        bool shouldBeClickThrough = !bShowMenu;
        if (shouldBeClickThrough != isClickThrough) {
            LONG style = GetWindowLong(g_hwndOverlay, GWL_EXSTYLE);
            if (shouldBeClickThrough) {
                SetWindowLong(g_hwndOverlay, GWL_EXSTYLE, style | WS_EX_TRANSPARENT);
            }
            else {
                SetWindowLong(g_hwndOverlay, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
            }
            isClickThrough = shouldBeClickThrough;
        }

        if (settings::bStreamerMode) SetWindowDisplayAffinity(g_hwndOverlay, WDA_EXCLUDEFROMCAPTURE); else SetWindowDisplayAffinity(g_hwndOverlay, WDA_NONE);

        const uint64_t viewGen = shared::viewGeneration.load(std::memory_order_relaxed);
        const uint64_t entityGen = shared::entityGeneration.load(std::memory_order_relaxed);
        UpdatePerfCounters(lastSeenViewGen, lastSeenEntityGen, effectiveDrawFrames, loopFrames);
        lastSeenViewGen = viewGen;
        lastSeenEntityGen = entityGen;

        // Dynamic font reload for ESP text (compatibility options + size)
        static int lastFontIdx = -1;
        static float lastFontSz = -1.0f;
        if (settings::espFontIndex != lastFontIdx || settings::espFontSize != lastFontSz || g_fontReloadNeeded) {
            LoadESPFont();
            lastFontIdx = settings::espFontIndex;
            lastFontSz = settings::espFontSize;
            g_fontReloadNeeded = false;
        }

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); ImDrawList* bg = ImGui::GetBackgroundDrawList();

        // Only use the selectable ESP font for overlay drawings (watermark, ESP elements, notifications).
        // The ImGui menu always uses the first loaded Chinese font (msyh) to avoid ? for Chinese text.
        if (g_espFont) ImGui::PushFont(g_espFont);
        DrawWatermark(bg);
        if (settings::bEulaAccepted) { RenderESP(bg); }
        RenderNotifications(bg);
        if (g_espFont) ImGui::PopFont();

        float dt = ImGui::GetIO().DeltaTime;
        bool displayMenu = bShowMenu;

        if (displayMenu) { menuAlpha += 10.0f * dt; if (menuAlpha > 1.0f) menuAlpha = 1.0f; }
        else { menuAlpha -= 10.0f * dt; if (menuAlpha < 0.0f) menuAlpha = 0.0f; }

        if (menuAlpha > 0.01f) {
            ImGui::GetStyle().Alpha = menuAlpha;
            bg->AddRectFilled(ImVec2(0, 0), ImVec2((float)ScreenWidth, (float)ScreenHeight), ImColor(0, 0, 0, (int)(150 * menuAlpha)));
            static ImVec2 menuPos(12.0f, 12.0f);
            ImGui::SetNextWindowSize(ImVec2(850, 500));
            ImGui::SetNextWindowPos(menuPos, ImGuiCond_Always);
            ImGui::Begin("JANG_WT", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
            ImVec2 s = ImGui::GetWindowSize();
            ImGui::SetCursorPos(ImVec2(0, 0));
            ImGui::InvisibleButton("##MenuDrag", ImVec2(s.x, 40));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                menuPos.x += ImGui::GetIO().MouseDelta.x;
                menuPos.y += ImGui::GetIO().MouseDelta.y;
            }
            else {
                menuPos = ImGui::GetWindowPos();
            }
            ImVec2 p = ImGui::GetWindowPos(); ImDrawList* draw = ImGui::GetWindowDrawList();
            UI::DrawGlow(draw, p, ImVec2(p.x + s.x, p.y + s.y), ImColor(219, 44, 44, (int)(255 * menuAlpha)), 12.0f, 20.0f);
            draw->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), ImColor(17, 17, 21, (int)(255 * menuAlpha)), 12.0f);
            draw->AddRectFilled(p, ImVec2(p.x + 180, p.y + s.y), ImColor(12, 12, 15, (int)(255 * menuAlpha)), 12.0f, ImDrawFlags_RoundCornersLeft);
            ImGui::SetCursorPos(ImVec2(20, 30)); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, menuAlpha), "JANG"); ImGui::SetCursorPos(ImVec2(20, 60)); ImGui::TextDisabled("战争雷霆 DMA");

            if (settings::bEulaAccepted) {
                ImGui::SetCursorPos(ImVec2(15, 100)); ImGui::BeginGroup();
                if (UI::Tab("仪表盘", activeTab == 0)) activeTab = 0;
                if (UI::Tab("战斗", activeTab == 1)) activeTab = 1;
                if (UI::Tab("视觉", activeTab == 2)) activeTab = 2;
                if (UI::Tab("功能", activeTab == 3)) activeTab = 3;
                if (UI::Tab("设置", activeTab == 4)) activeTab = 4;
                ImGui::EndGroup();
            }
            ImGui::SetCursorPos(ImVec2(20, 460)); ImGui::TextDisabled("状态:"); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, menuAlpha), "已注入");
            ImGui::SetCursorPos(ImVec2(200, 20)); ImGui::BeginChild("MainArea", ImVec2(630, 470), false, ImGuiWindowFlags_NoBackground);

            if (!settings::bEulaAccepted) {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "许可协议"); ImGui::Separator(); ImGui::Spacing();
                ImGui::BeginChild("EULA_Text", ImVec2(0, 350), true); ImGui::TextWrapped("欢迎使用 JANG 战争雷霆客户端。"); ImGui::Spacing(); ImGui::TextWrapped("原生引擎写入（功能页）风险较高，请自行承担使用后果。"); ImGui::EndChild();
                ImGui::Spacing(); ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 200) / 2);
                if (UI::Button("我已了解并接受", ImVec2(200, 40))) { settings::bEulaAccepted = true; std::ofstream f("license.dat"); f << "ACCEPTED"; f.close(); PushNotification("欢迎使用仪表盘。"); }
            }
            else {
                if (activeTab == 0) {
                    ImGui::Text("仪表盘"); ImGui::Separator(); ImGui::Spacing();
                    ImGui::Text("模式: DMA 副机");
                    ImGui::Text("设备: %s", settings::dmaDevice.c_str());
                    ImGui::Text("目标进程: %lu", mem.ProcessID);
                    ImGui::Text("基址: 0x%llX", (unsigned long long)mem.BaseAddress);
                    ImGui::Text("游戏: %dx%d (%s)", mem.GameScreenWidth, mem.GameScreenHeight, mem.resolutionSource.c_str());
                    ImGui::Text("ESP 窗口: %dx%d @ (%d,%d)", ScreenWidth, ScreenHeight, mem.LastRect.left, mem.LastRect.top);
                    ImGui::Text("绘制帧率: %u | 视图: %u | 数据: %u | 循环: %u | 缓存: %ums",
                        perf::drawFps.load(), perf::viewFps.load(), perf::entityFps.load(), perf::loopFps.load(), perf::cacheMs.load());
                    ImGui::Text("弹速: %.0f m/s | 预测:%s 坠落:%s g=%.2f", 
                        shared::LiveVelocity,
                        settings::bPrediction ? "ON" : "OFF",
                        settings::bBulletDrop ? "ON" : "OFF",
                        settings::gravityScale);
                    ImGui::Text("中文名: %s | Kmbox: %s", 
                        settings::bForceChineseNames ? "强制" : "自动",
                        Input::IsReady() ? "已连接" : (settings::bUseKmbox ? "失败" : "已禁用"));
                    if (!offsets::api_version.empty()) ImGui::Text("偏移: v%s", offsets::api_version.c_str());
                }
                else if (activeTab == 1) {
                    ImGui::Columns(2, nullptr, false); ImGui::BeginChild("Aim", ImVec2(0, 0), true); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "瞄准辅助"); ImGui::Separator();
                    UI::Toggle("内存自瞄", &settings::bMemoryAim);
                    UI::Toggle("显示视野圈", &settings::bShowFovCircle);
                    if (settings::bMemoryAim) { int k = settings::aimKey; if (ImGui::Combo("按键", &k, "左键\0右键\0Alt\0Shift\0")) settings::aimKey = (k == 0 ? VK_LBUTTON : (k == 1 ? VK_RBUTTON : (k == 2 ? VK_MENU : VK_SHIFT))); UI::Slider("平滑度", &settings::aimSmooth, 1.0f, 20.0f); UI::Slider("视野范围", &settings::aimFov, 10.0f, 500.0f); }
                    ImGui::EndChild(); ImGui::NextColumn(); ImGui::BeginChild("Pred", ImVec2(0, 0), true); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "弹道预测"); ImGui::Separator();
                    UI::Toggle("速度预测", &settings::bPrediction); UI::Toggle("子弹下坠", &settings::bBulletDrop); if (settings::bBulletDrop) UI::Slider("重力系数", &settings::gravityScale, 0.50f, 1.20f, "%.2f"); ImGui::Separator(); ImGui::EndChild(); ImGui::Columns(1);
                }
                else if (activeTab == 2) {
                    ImGui::Columns(2, nullptr, false); ImGui::BeginChild("Esp", ImVec2(0, 0), true); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "玩家视觉"); ImGui::Separator();
                    UI::Toggle("ESP 总开关", &settings::bEsp);
                    if (settings::bEsp) {
                        ImGui::Indent(15.0f);

                        // 过滤
                        ImGui::TextDisabled("过滤");
                        UI::Toggle("BOT ESP", &settings::bEspBots);
                        UI::Toggle("队友 ESP", &settings::bEspTeammates);
                        if (settings::bEspTeammates) {
                            ImGui::Indent(15.0f);
                            ImGui::ColorEdit4("##TeamBoxColor", settings::col_BoxTeam, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                            ImGui::SameLine(); ImGui::Text("队友颜色");
                            ImGui::Unindent(15.0f);
                        }
                        UI::Toggle("地面目标", &settings::bEspGround);
                        UI::Toggle("强制中文名称", &settings::bForceChineseNames);

                        // 方框
                        ImGui::TextDisabled("方框");
                        UI::Toggle("2D 方框", &settings::bBox);
                        if (settings::bBox) { ImGui::Indent(15.0f); UI::Toggle("填充背景", &settings::bFilledBox); ImGui::ColorEdit4("##BoxColor", settings::col_BoxVis, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel); ImGui::SameLine(); ImGui::Text("敌方颜色"); ImGui::Unindent(15.0f); }
                        UI::Toggle("3D 方框", &settings::bBox3D);

                        // 信息
                        ImGui::TextDisabled("信息");
                        UI::Toggle("名称", &settings::bName); UI::Toggle("距离", &settings::bDistance); UI::Toggle("装填进度", &settings::bReloadBar); UI::Toggle("朝向", &settings::bFacing); UI::Toggle("连线", &settings::bLines);

                        // 高级
                        ImGui::TextDisabled("高级");
                        UI::Toggle("坦克内部 (透视)", &settings::bInternalsESP);
                        if (settings::bInternalsESP) {
                            ImGui::Indent(15.0f);
                            ImGui::Combo("绘制模式", &settings::iInternalsMode, "3D 边界框\0半透明网格\0");
                            UI::Toggle("显示部件名称", &settings::bInternalsName);
                            ImGui::Unindent(15.0f);
                        }

                        // 字体 (ESP 文字)
                        ImGui::Separator();
                        ImGui::TextDisabled("ESP 文字");
                        const char* fontNames[] = {
                            "微软雅黑", "黑体", "宋体",
                            "Consolas", "Arial", "Segoe UI", "Verdana", "Calibri", "Tahoma", "Courier New",
                            "默认"
                        };
                        ImGui::Combo("字体", &settings::espFontIndex, fontNames, IM_ARRAYSIZE(fontNames));
                        UI::Slider("字号", &settings::espFontSize, 10.0f, 36.0f, "%.0f");
                        ImGui::ColorEdit4("##ESPTextColor", settings::col_ESPText, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                        ImGui::SameLine(); ImGui::Text("文字颜色");

                        ImGui::Combo("位置标记样式", &settings::espMarkerStyle, "小点\0实心圆\0空心圆\0菱形\0小方块\0", 5);

                        ImGui::Unindent(15.0f);
                    }
                    ImGui::EndChild(); ImGui::NextColumn(); ImGui::BeginChild("Env", ImVec2(0, 0), true); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "环境"); ImGui::Separator();
                    UI::Toggle("导弹 ESP 与告警", &settings::bMissileESP); 
                    UI::Toggle("炸弹 CCIP", &settings::bCCIP);
                    UI::Toggle("空中提前量", &settings::bAirLead);
                    ImGui::Separator(); ImGui::EndChild(); ImGui::Columns(1);
                }
                else if (activeTab == 3) {
                    ImGui::BeginChild("Exp", ImVec2(0, 0), true);
                    ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "内存写入功能"); ImGui::Separator();
                    ImGui::TextWrapped("以下选项会写入游戏内存，默认全部关闭。");
                    ImGui::Spacing();
                    if (UI::Toggle("启用内存写入 (总开关)", &settings::bEnableMemoryWrites)) {
                        if (!settings::bEnableMemoryWrites) {
                            settings::bMindControlActive = false;
                            shared::TargetHijackPtr = 0;
                            PushNotification("内存写入已关闭。");
                        }
                    }

                    if (!settings::bEnableMemoryWrites) ImGui::BeginDisabled();

                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "HUD 覆盖"); ImGui::Separator();
                    UI::Toggle("强制街机准星", &settings::bForceArcadeCrosshair);
                    UI::Toggle("强制空中提前量 UI", &settings::bForceAirLead);
                    UI::Toggle("强制坦克 ESP", &settings::bForceTankESP);
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "单位修改"); ImGui::Separator();
                    UI::Toggle("强制夜视/热成像", &settings::bForceThermals);
                    UI::Toggle("强制空中装填 (飞机)", &settings::bMidAirReload);
                    UI::Toggle("幽灵履带 (无碰撞/碎片减速)", &settings::bGhostCollision);
                    UI::Toggle("零冷却侦察 Spam", &settings::bSpamScout);
                    UI::Toggle("街机引擎倍率", &settings::bThrustMult);
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "实体劫持"); ImGui::Separator();
                    if (UI::Toggle("启用实体劫持", &settings::bEnableEntityHijack)) {
                        if (!settings::bEnableEntityHijack) settings::bMindControlActive = false;
                    }
                    if (!settings::bEnableEntityHijack) ImGui::BeginDisabled();

                    std::vector<CachedEntity> uiEntities;
                    {
                        std::lock_guard<std::mutex> lock(shared::DataMutex);
                        uiEntities = shared::Entities;
                    }

                    static int selectedEnt = -1;
                    std::string preview = "选择目标...";
                    if (selectedEnt >= 0 && selectedEnt < (int)uiEntities.size()) {
                        preview = std::string("[") +
                            (uiEntities[selectedEnt].team == shared::LocalTeam ? "队友" : "敌方") +
                            "] " + uiEntities[selectedEnt].name;
                    }
                    else {
                        selectedEnt = -1;
                    }

                    if (ImGui::BeginCombo("##HijackCombo", preview.c_str())) {
                        for (int i = 0; i < (int)uiEntities.size(); i++) {
                            const std::string label = std::string("[") +
                                (uiEntities[i].team == shared::LocalTeam ? "队友" : "敌方") +
                                "] " + uiEntities[i].name;
                            bool is_selected = (selectedEnt == i);
                            if (ImGui::Selectable(label.c_str(), is_selected)) {
                                selectedEnt = i;
                                shared::TargetHijackPtr = uiEntities[i].pointer;
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::Spacing();
                    if (UI::Button("劫持选中", ImVec2(180, 30))) {
                        if (settings::bEnableMemoryWrites && settings::bEnableEntityHijack && shared::TargetHijackPtr != 0) {
                            settings::bMindControlActive = true;
                            PushNotification("正在劫持实体...");
                        }
                    }
                    ImGui::SameLine();
                    if (UI::Button("归还控制", ImVec2(180, 30))) {
                        settings::bMindControlActive = false;
                        shared::TargetHijackPtr = 0;
                        selectedEnt = -1;
                        PushNotification("已归还本地玩家控制。");
                    }

                    if (settings::bMindControlActive) {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "当前正在劫持实体！");
                    }

                    if (!settings::bEnableEntityHijack) ImGui::EndDisabled();
                    if (!settings::bEnableMemoryWrites) ImGui::EndDisabled();

                    ImGui::EndChild();
                }
                else if (activeTab == 4) {
                    ImGui::BeginChild("Cfg", ImVec2(0, 0), true); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "系统与配置"); ImGui::Separator();
                    ImGui::TextDisabled("修改 dma_config.ini 后重启以应用 DMA/Kmbox/覆盖层设置。");
                    ImGui::Text("游戏: %dx%d (%s)", mem.GameScreenWidth, mem.GameScreenHeight, mem.resolutionSource.c_str());
                    ImGui::Text("ESP 窗口: %dx%d @ (%d,%d)", ScreenWidth, ScreenHeight, settings::overlayX, settings::overlayY);
                    ImGui::Text("自动分辨率: %s", settings::overlayAutoResolution ? "开启" : "关闭");
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    UI::Toggle("自动识别队伍", &settings::bAutoTeam); if (!settings::bAutoTeam) { ImGui::Indent(15.0f); ImGui::SliderInt("手动队伍 ID", &settings::ManualTeam, 0, 4); ImGui::Unindent(15.0f); }
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    if (UI::Button("主播模式", ImVec2(-1, 40))) { settings::bStreamerMode = !settings::bStreamerMode; PushNotification("主播模式已切换"); } ImGui::Spacing(); if (UI::Button("卸载", ImVec2(-1, 40))) { app::running.store(false, std::memory_order_relaxed); } ImGui::EndChild();
                }
            }
            ImGui::EndChild(); ImGui::End();
        }
        ImGui::Render();
        const float cc[4] = { 0.02f, 0.02f, 0.02f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(0, 0);

        // Light yield to keep the window responsive and reduce CPU spin / DMA mutex starvation.
        // When menu is hidden (pure ESP) we can afford slightly lower tick rate; when menu shown keep snappier.
        if (!bShowMenu) {
            Sleep(1);   // ~ cap ~500-1000 fps, vastly reduces busy loop while keeping smooth visuals
        } else {
            Sleep(0);   // yield remainder of slice when menu open
        }
    }

    ShutdownApplication();
    return 0;
}