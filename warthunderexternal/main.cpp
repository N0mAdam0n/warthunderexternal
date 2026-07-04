#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <sstream> 
#include <fstream>
#include <time.h>
#include <dwmapi.h>
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
#pragma comment(lib, "dwmapi.lib")

Memory mem;
int ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
int ScreenHeight = GetSystemMetrics(SM_CYSCREEN);
HWND g_hwndOverlay = NULL;
bool bShowMenu = true;

namespace shared {
    std::vector<CachedEntity> Entities;
    std::vector<CachedRocket> Rockets;
    Matrix4x4 ViewMatrix;
    Vector3 LocalPos;
    Vector3 NativePredictionPos;
    Vector3 CCIPPos;
    float LiveVelocity;
    int LocalTeam;
    std::mutex DataMutex;
    std::atomic<uintptr_t> TargetHijackPtr(0);
    std::atomic<uint64_t> viewGeneration(0);
    std::atomic<uint64_t> entityGeneration(0);
    std::atomic<uint64_t> entityCacheTick(0);
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
    bool bShowFovCircle = true;
    bool bPrediction = true;
    bool bBulletDrop = true;
    float gravityScale = 1.0f;
    bool bUseNativePredictionAim = false;
    float targetHeightRatio = 0.5f;

    bool bEsp = true;
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
    float col_Fov[4] = { 1.0f, 1.0f, 1.0f, 0.5f };
}

extern void CacheThread();
extern void FastViewThread();
extern void RenderESP(ImDrawList* draw);

struct Particle { float x, y, dx, dy, size, alpha; };
std::vector<Particle> g_Particles;

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
            draw->AddText(ImVec2(pos.x + padding.x, pos.y + padding.y), ImColor(255, 255, 255, (int)(255 * it->alpha)), it->message.c_str());
            startY -= (boxSize.y + 10.0f);
        }
        if (it->timer <= 0.0f) it = g_Notifications.erase(it); else ++it;
    }
}

void DrawWatermark(ImDrawList* draw) {
    time_t raw; struct tm info; char buf[80]; time(&raw); localtime_s(&info, &raw); strftime(buf, sizeof(buf), "%H:%M:%S", &info);
    char text[192];
    sprintf_s(text, "JANG DMA (WT) | Draw: %u | View: %u | Data: %u | Cache: %ums | %s",
        perf::drawFps.load(), perf::viewFps.load(), perf::entityFps.load(), perf::cacheMs.load(), buf);
    ImVec2 pos(20, 20); ImVec2 tSize = ImGui::CalcTextSize(text); ImVec2 pad(12, 6);
    ImVec2 bSize(tSize.x + pad.x * 2, tSize.y + pad.y * 2);
    draw->AddRectFilled(pos, ImVec2(pos.x + bSize.x, pos.y + bSize.y), ImColor(15, 15, 15, 200), 4.0f);
    draw->AddRectFilled(pos, ImVec2(pos.x + bSize.x, pos.y + 2), ImColor(219, 44, 44, 255), 4.0f, ImDrawFlags_RoundCornersTop);
    draw->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), ImColor(255, 255, 255), text);
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
    ImGuiIO& io = ImGui::GetIO(); ImFontConfig config; config.OversampleH = 2; config.OversampleV = 2;
    if (std::ifstream("C:\\Windows\\Fonts\\segoeui.ttf").good()) io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &config);
    else io.Fonts->AddFontDefault();
}

static bool EnsureOverlayDimensions() {
    if (ScreenWidth < 100 || ScreenHeight < 100) {
        ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
        ScreenHeight = GetSystemMetrics(SM_CYSCREEN);
    }
    return ScreenWidth >= 100 && ScreenHeight >= 100;
}

bool InitOverlay() {
    mem.UpdateGameWindow();
    if (!EnsureOverlayDimensions()) {
        std::cout << "[!] Invalid overlay size." << std::endl;
        return false;
    }

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, L"SaturnClass", NULL };
    RegisterClassExW(&wc);
    g_hwndOverlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"SaturnClass", L"SaturnOverlay", WS_POPUP,
        mem.LastRect.left, mem.LastRect.top, ScreenWidth, ScreenHeight,
        NULL, NULL, wc.hInstance, NULL);
    if (!g_hwndOverlay) {
        std::cout << "[!] Failed to create overlay window. Error=" << GetLastError() << std::endl;
        return false;
    }

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_hwndOverlay, &margins);

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
    ShowWindow(g_hwndOverlay, SW_SHOW);
    UpdateWindow(g_hwndOverlay);
    SetForegroundWindow(g_hwndOverlay);

    for (int i = 0; i < 50; i++) {
        g_Particles.push_back({
            (float)(rand() % ScreenWidth), (float)(rand() % ScreenHeight),
            ((rand() % 100) - 50) / 200.0f, 0,
            (float)(rand() % 2 + 1), (float)(rand() % 100) / 100.0f
        });
    }
    return true;
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleTitleA("JANG DMA Loader [WT EDITION]");
    std::cout << "[>] Initializing DMA edition..." << std::endl;
    std::ifstream f("license.dat"); if (f.good()) settings::bEulaAccepted = true;

    if (LoadDmaConfig()) {
        std::cout << " [+] Loaded dma_config.ini" << std::endl;
    }
    else {
        std::cout << "[!] dma_config.ini not found, using defaults." << std::endl;
    }

    std::cout << "[>] Connecting to DMA device..." << std::endl;
    if (!mem.Connect()) {
        std::cout << "[!] Failed to connect to DMA device." << std::endl;
        return 1;
    }

    if (settings::bUseKmbox) {
        Input::Init();
    }

    std::cout << "[>] Waiting for aces.exe on target machine... (Press END to cancel)" << std::endl;
    while (mem.GetPID(L"aces.exe") == 0) {
        if (GetAsyncKeyState(VK_END) & 1) return 0;
        Sleep(1000);
    }
    std::cout << " [+] Found aces.exe!" << std::endl;

    std::cout << " [>] Attaching to process..." << std::endl;
    while (!mem.Attach("aces.exe")) {
        if (GetAsyncKeyState(VK_END) & 1) return 0;
        Sleep(1000);
    }
    std::cout << " [+] Attached successfully! Fetching offsets from API..." << std::endl;

    if (!mem.UpdateOffsets()) {
        std::cout << "[!] Using built-in offset defaults." << std::endl;
    }

    std::cout << "[>] Overlay align mode: " << settings::overlayAlignMode << std::endl;
    if (settings::overlayAlignMode != "manual") {
        if (!settings::captureWindowTitle.empty()) {
            std::cout << " [>] Looking for capture window: " << settings::captureWindowTitle << std::endl;
        }
        else if (settings::overlayAutoCapture) {
            std::cout << " [>] Auto-detecting capture preview window (OBS/PotPlayer/etc.)" << std::endl;
        }
    }

    if (!InitOverlay()) {
        std::cout << "[!] Overlay initialization failed." << std::endl;
        return 1;
    }
    std::cout << " [+] Overlay " << ScreenWidth << "x" << ScreenHeight
        << " @ (" << mem.LastRect.left << "," << mem.LastRect.top << ")"
        << " source=" << mem.overlayAlignSource << std::endl;
    if (mem.GameHwnd == NULL && settings::overlayAlignMode != "manual") {
        std::cout << "[!] Capture window not found. Set capture_window in dma_config.ini to your preview window title." << std::endl;
        LogVisibleCaptureCandidates();
    }

    std::thread(FastViewThread).detach();
    std::thread(CacheThread).detach();

    float menuAlpha = 0.0f; int activeTab = 0;
    LONG exStyle = GetWindowLong(g_hwndOverlay, GWL_EXSTYLE);
    bool isClickThrough = !bShowMenu;
    uint64_t lastSeenViewGen = 0;
    uint64_t lastSeenEntityGen = 0;
    uint64_t effectiveDrawFrames = 0;
    uint64_t loopFrames = 0;

    if (bShowMenu) { SetWindowLong(g_hwndOverlay, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT); SetForegroundWindow(g_hwndOverlay); }
    else { SetWindowLong(g_hwndOverlay, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT); }

    while (true) {
        MSG msg; while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); if (msg.message == WM_QUIT) return 0; }
        if (GetAsyncKeyState(VK_END) & 1) break;

        mem.UpdateGameWindow();

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

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); ImDrawList* bg = ImGui::GetBackgroundDrawList();

        DrawWatermark(bg);
        if (settings::bEulaAccepted) { RenderESP(bg); }
        RenderNotifications(bg);

        float dt = ImGui::GetIO().DeltaTime;
        bool displayMenu = bShowMenu;

        if (displayMenu) { menuAlpha += 10.0f * dt; if (menuAlpha > 1.0f) menuAlpha = 1.0f; }
        else { menuAlpha -= 10.0f * dt; if (menuAlpha < 0.0f) menuAlpha = 0.0f; }

        if (menuAlpha > 0.01f) {
            ImGui::GetStyle().Alpha = menuAlpha;
            bg->AddRectFilled(ImVec2(0, 0), ImVec2((float)ScreenWidth, (float)ScreenHeight), ImColor(0, 0, 0, (int)(150 * menuAlpha)));
            for (auto& p : g_Particles) {
                p.y -= 0.8f; p.x += p.dx; p.alpha -= 0.003f;
                if (p.y < 0 || p.alpha <= 0.0f) { p.x = (float)(rand() % ScreenWidth); p.y = (float)ScreenHeight; p.dx = ((rand() % 100) - 50) / 200.0f; p.alpha = 1.0f; }
                float ca = p.alpha * menuAlpha; bg->AddCircleFilled(ImVec2(p.x, p.y), p.size, ImColor(219, 44, 44, (int)(ca * 180))); UI::DrawGlow(bg, ImVec2(p.x, p.y), ImVec2(p.x, p.y), ImColor(219, 44, 44, (int)(ca * 50)), p.size * 2.0f, 5.0f);
            }
            ImGui::SetNextWindowSize(ImVec2(850, 500));
            ImGui::Begin("JANG_WT", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings);
            ImVec2 p = ImGui::GetWindowPos(); ImVec2 s = ImGui::GetWindowSize(); ImDrawList* draw = ImGui::GetWindowDrawList();
            UI::DrawGlow(draw, p, ImVec2(p.x + s.x, p.y + s.y), ImColor(219, 44, 44, (int)(255 * menuAlpha)), 12.0f, 20.0f);
            draw->AddRectFilled(p, ImVec2(p.x + s.x, p.y + s.y), ImColor(17, 17, 21, (int)(255 * menuAlpha)), 12.0f);
            draw->AddRectFilled(p, ImVec2(p.x + 180, p.y + s.y), ImColor(12, 12, 15, (int)(255 * menuAlpha)), 12.0f, ImDrawFlags_RoundCornersLeft);
            ImGui::SetCursorPos(ImVec2(20, 30)); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, menuAlpha), "JANG"); ImGui::SetCursorPos(ImVec2(20, 60)); ImGui::TextDisabled("WAR THUNDER DMA");

            if (settings::bEulaAccepted) {
                ImGui::SetCursorPos(ImVec2(15, 100)); ImGui::BeginGroup();
                if (UI::Tab("DASHBOARD", activeTab == 0)) activeTab = 0;
                if (UI::Tab("COMBAT", activeTab == 1)) activeTab = 1;
                if (UI::Tab("VISUALS", activeTab == 2)) activeTab = 2;
                if (UI::Tab("EXPLOITS", activeTab == 3)) activeTab = 3;
                if (UI::Tab("SETTINGS", activeTab == 4)) activeTab = 4;
                ImGui::EndGroup();
            }
            ImGui::SetCursorPos(ImVec2(20, 460)); ImGui::TextDisabled("STATUS:"); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, menuAlpha), "INJECTED");
            ImGui::SetCursorPos(ImVec2(200, 20)); ImGui::BeginChild("MainArea", ImVec2(630, 470), false, ImGuiWindowFlags_NoBackground);

            if (!settings::bEulaAccepted) {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "LICENSE AGREEMENT"); ImGui::Separator(); ImGui::Spacing();
                ImGui::BeginChild("EULA_Text", ImVec2(0, 350), true); ImGui::TextWrapped("Welcome to JANG Client for War Thunder."); ImGui::Spacing(); ImGui::TextWrapped("Native Engine writes (Exploits tab) carry high risk. Use at your own risk."); ImGui::EndChild();
                ImGui::Spacing(); ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 200) / 2);
                if (UI::Button("I UNDERSTAND & ACCEPT", ImVec2(200, 40))) { settings::bEulaAccepted = true; std::ofstream f("license.dat"); f << "ACCEPTED"; f.close(); PushNotification("Welcome to Dashboard."); }
            }
            else {
                if (activeTab == 0) {
                    ImGui::Text("DASHBOARD"); ImGui::Separator(); ImGui::Spacing();
                    ImGui::Text("Mode: DMA Secondary Machine");
                    ImGui::Text("Device: %s", settings::dmaDevice.c_str());
                    ImGui::Text("Target PID: %lu", mem.ProcessID);
                    ImGui::Text("Base: 0x%llX", (unsigned long long)mem.BaseAddress);
                    ImGui::Text("Overlay: %dx%d @ (%d,%d)", ScreenWidth, ScreenHeight, mem.LastRect.left, mem.LastRect.top);
                    ImGui::Text("Align: %s", mem.overlayAlignSource.c_str());
                    ImGui::Text("Draw FPS: %u | View: %u | Data: %u | Loop: %u | Cache: %ums",
                        perf::drawFps.load(), perf::viewFps.load(), perf::entityFps.load(), perf::loopFps.load(), perf::cacheMs.load());
                    if (mem.GameHwnd) {
                        char title[128] = {};
                        GetWindowTextA(mem.GameHwnd, title, sizeof(title));
                        ImGui::Text("Target: %s", title);
                    }
                    ImGui::Text("Kmbox: %s", Input::IsReady() ? "Connected" : (settings::bUseKmbox ? "Failed" : "Disabled"));
                    if (!offsets::api_version.empty()) ImGui::Text("Offsets: v%s", offsets::api_version.c_str());
                }
                else if (activeTab == 1) {
                    ImGui::Columns(2, nullptr, false); ImGui::BeginChild("Aim", ImVec2(0, 0), true); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "AIM ASSIST"); ImGui::Separator();
                    UI::Toggle("Memory Aimbot", &settings::bMemoryAim);
                    if (settings::bMemoryAim) { int k = settings::aimKey; if (ImGui::Combo("Key", &k, "LMB\0RMB\0Alt\0Shift\0")) settings::aimKey = (k == 0 ? VK_LBUTTON : (k == 1 ? VK_RBUTTON : (k == 2 ? VK_MENU : VK_SHIFT))); UI::Slider("Smoothing", &settings::aimSmooth, 1.0f, 20.0f); UI::Slider("FOV", &settings::aimFov, 10.0f, 500.0f); UI::Toggle("Show FOV", &settings::bShowFovCircle); }
                    ImGui::EndChild(); ImGui::NextColumn(); ImGui::BeginChild("Pred", ImVec2(0, 0), true); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "PREDICTION"); ImGui::Separator();
                    UI::Toggle("Velocity Prediction", &settings::bPrediction); UI::Toggle("Bullet Drop", &settings::bBulletDrop); if (settings::bBulletDrop) UI::Slider("Grav", &settings::gravityScale, 1.05f, 1.15f, "%.4f"); ImGui::Separator(); ImGui::EndChild(); ImGui::Columns(1);
                }
                else if (activeTab == 2) {
                    ImGui::Columns(2, nullptr, false); ImGui::BeginChild("Esp", ImVec2(0, 0), true); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "PLAYER VISUALS"); ImGui::Separator();
                    UI::Toggle("Master ESP", &settings::bEsp);
                    if (settings::bEsp) {
                        UI::Toggle("2D Box", &settings::bBox);
                        if (settings::bBox) { ImGui::Indent(15.0f); UI::Toggle("Filled Background", &settings::bFilledBox); ImGui::ColorEdit4("Color", settings::col_BoxVis, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel); ImGui::SameLine(); ImGui::Text("Box Color"); ImGui::Unindent(15.0f); }
                        UI::Toggle("3D Box", &settings::bBox3D); UI::Toggle("Names", &settings::bName); UI::Toggle("Distance", &settings::bDistance); UI::Toggle("Reload Bar", &settings::bReloadBar); UI::Toggle("Facing", &settings::bFacing); UI::Toggle("Snaplines", &settings::bLines);

                        UI::Toggle("Tank Internals (X-Ray)", &settings::bInternalsESP);
                        if (settings::bInternalsESP) {
                            ImGui::Indent(15.0f);
                            ImGui::Combo("Draw Mode", &settings::iInternalsMode, "3D Bounding Box\0Translucent Mesh\0");
                            UI::Toggle("Show Part Names", &settings::bInternalsName);
                            ImGui::Unindent(15.0f);
                        }
                    }
                    ImGui::EndChild(); ImGui::NextColumn(); ImGui::BeginChild("Env", ImVec2(0, 0), true); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "ENVIRONMENT"); ImGui::Separator();
                    UI::Toggle("Missile ESP & MAWS", &settings::bMissileESP); 
                    UI::Toggle("Bomb CCIP", &settings::bCCIP);
                    UI::Toggle("Air Lead", &settings::bAirLead);
                    ImGui::Separator(); ImGui::EndChild(); ImGui::Columns(1);
                }
                else if (activeTab == 3) {
                    ImGui::BeginChild("Exp", ImVec2(0, 0), true);
                    ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "MEMORY WRITE FEATURES"); ImGui::Separator();
                    ImGui::TextWrapped("All options below write to game memory. Disabled by default.");
                    ImGui::Spacing();
                    if (UI::Toggle("Enable Memory Writes (Master)", &settings::bEnableMemoryWrites)) {
                        if (!settings::bEnableMemoryWrites) {
                            settings::bMindControlActive = false;
                            shared::TargetHijackPtr = 0;
                            PushNotification("Memory writes disabled.");
                        }
                    }

                    if (!settings::bEnableMemoryWrites) ImGui::BeginDisabled();

                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "HUD OVERRIDES"); ImGui::Separator();
                    UI::Toggle("Force Arcade Crosshair", &settings::bForceArcadeCrosshair);
                    UI::Toggle("Force Air Lead UI", &settings::bForceAirLead);
                    UI::Toggle("Force Tank ESP", &settings::bForceTankESP);
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "UNIT MODIFIERS"); ImGui::Separator();
                    UI::Toggle("Force NVD/Thermals", &settings::bForceThermals);
                    UI::Toggle("Force Mid-Air Reload (Planes)", &settings::bMidAirReload);
                    UI::Toggle("Ghost Tracks (No Collision/Debris Slow)", &settings::bGhostCollision);
                    UI::Toggle("0-Cooldown Scout Spam", &settings::bSpamScout);
                    UI::Toggle("Arcade Engine Multiplier", &settings::bThrustMult);
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                    ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "ENTITY HIJACKING"); ImGui::Separator();
                    if (UI::Toggle("Enable Entity Hijack", &settings::bEnableEntityHijack)) {
                        if (!settings::bEnableEntityHijack) settings::bMindControlActive = false;
                    }
                    if (!settings::bEnableEntityHijack) ImGui::BeginDisabled();

                    shared::DataMutex.lock();
                    std::vector<CachedEntity> uiEntities = shared::Entities;
                    shared::DataMutex.unlock();

                    static int selectedEnt = -1;
                    std::string preview = "Select Target...";
                    if (selectedEnt >= 0 && selectedEnt < (int)uiEntities.size()) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "[%s] %s", uiEntities[selectedEnt].team == shared::LocalTeam ? "TEAM" : "ENEMY", uiEntities[selectedEnt].name.c_str());
                        preview = buf;
                    }
                    else {
                        selectedEnt = -1;
                    }

                    if (ImGui::BeginCombo("##HijackCombo", preview.c_str())) {
                        for (int i = 0; i < (int)uiEntities.size(); i++) {
                            char buf[128];
                            snprintf(buf, sizeof(buf), "[%s] %s", uiEntities[i].team == shared::LocalTeam ? "TEAM" : "ENEMY", uiEntities[i].name.c_str());
                            bool is_selected = (selectedEnt == i);
                            if (ImGui::Selectable(buf, is_selected)) {
                                selectedEnt = i;
                                shared::TargetHijackPtr = uiEntities[i].pointer;
                            }
                            if (is_selected) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::Spacing();
                    if (UI::Button("HIJACK SELECTED", ImVec2(180, 30))) {
                        if (settings::bEnableMemoryWrites && settings::bEnableEntityHijack && shared::TargetHijackPtr != 0) {
                            settings::bMindControlActive = true;
                            PushNotification("Hijacking Entity...");
                        }
                    }
                    ImGui::SameLine();
                    if (UI::Button("RETURN CONTROL", ImVec2(180, 30))) {
                        settings::bMindControlActive = false;
                        shared::TargetHijackPtr = 0;
                        selectedEnt = -1;
                        PushNotification("Returned to Local Player.");
                    }

                    if (settings::bMindControlActive) {
                        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Currently Hijacking Entity!");
                    }

                    if (!settings::bEnableEntityHijack) ImGui::EndDisabled();
                    if (!settings::bEnableMemoryWrites) ImGui::EndDisabled();

                    ImGui::EndChild();
                }
                else if (activeTab == 4) {
                    ImGui::BeginChild("Cfg", ImVec2(0, 0), true); ImGui::TextColored(ImVec4(0.86f, 0.17f, 0.17f, 1.f), "SYSTEM & CONFIG"); ImGui::Separator();
                    ImGui::TextDisabled("Edit dma_config.ini and restart to apply DMA/Kmbox settings.");
                    ImGui::Text("Capture Window: %s", settings::captureWindowTitle.empty() ? "(fullscreen)" : settings::captureWindowTitle.c_str());
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    UI::Toggle("Auto Team Detect", &settings::bAutoTeam); if (!settings::bAutoTeam) { ImGui::Indent(15.0f); ImGui::SliderInt("Manual ID", &settings::ManualTeam, 0, 4); ImGui::Unindent(15.0f); }
                    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                    if (UI::Button("Streamer Mode", ImVec2(-1, 40))) { settings::bStreamerMode = !settings::bStreamerMode; PushNotification("Streamer Mode Toggled"); } ImGui::Spacing(); if (UI::Button("UNLOAD", ImVec2(-1, 40))) { Input::Shutdown(); mem.Disconnect(); exit(0); } ImGui::EndChild();
                }
            }
            ImGui::EndChild(); ImGui::End();
        }
        ImGui::Render(); float cc[4] = { 0,0,0,0 }; g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL); g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); g_pSwapChain->Present(0, 0);
    }
    return 0;
}