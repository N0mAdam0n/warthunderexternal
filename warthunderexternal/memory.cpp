#include "memory.hpp"
#include "config.hpp"
#include <iostream>
#include <cstring>

bool Memory::Connect() {
    if (g_dma.IsReady()) return true;

    const std::string folder = settings::dmaFolder.empty() ? "dma" : settings::dmaFolder;
    if (!g_dma.LoadLibraries(folder)) {
        std::cout << "[!] Failed to load DMA libraries." << std::endl;
        if (!g_dma.lastError.empty()) {
            std::cout << "     " << g_dma.lastError << std::endl;
        }
        return false;
    }

    const std::string device = settings::dmaDevice.empty() ? "fpga" : settings::dmaDevice;
    if (!g_dma.Initialize(device, true)) {
        std::cout << "[!] VMMDLL_Initialize failed. Check DMA device (" << device << ")." << std::endl;
        if (!g_dma.lastError.empty()) {
            std::cout << "     " << g_dma.lastError << std::endl;
        }
        return false;
    }

    std::cout << " [+] DMA connected via device: " << device << std::endl;
    return true;
}

void Memory::Disconnect() {
    g_dma.Shutdown();
    ProcessID = 0;
    BaseAddress = 0;
}

bool Memory::ReadBuffer(uintptr_t addr, void* buffer, size_t size) const {
    if (!g_dma.IsReady() || !ProcessID || !addr || !buffer || size == 0) return false;
    return g_dma.Read(ProcessID, addr, buffer, size);
}

std::string Memory::ReadString(uintptr_t addr, size_t maxLen) const {
    std::vector<char> buf(maxLen);
    ReadBuffer(addr, buf.data(), maxLen);
    buf[maxLen - 1] = 0;
    return std::string(buf.data());
}

DWORD Memory::GetPID(const std::wstring& procName) {
    if (!g_dma.IsReady()) return 0;
    std::string narrow(procName.begin(), procName.end());
    return g_dma.PidFromName(narrow);
}

bool Memory::Attach(const std::string& procName) {
    ProcessID = GetPID(std::wstring(procName.begin(), procName.end()));
    if (ProcessID == 0) return false;

    BaseAddress = static_cast<uintptr_t>(g_dma.ModuleBase(ProcessID, procName));
    return BaseAddress != 0;
}

bool Memory::UpdateOffsets() {
    return offsets::LoadFromApi();
}

static bool IsPlausibleResolution(int width, int height) {
    if (width < 640 || height < 480) return false;
    if (width > 7680 || height > 4320) return false;
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    return aspect >= 1.0f && aspect <= 3.5f;
}

static bool TryReadResolutionPair(const Memory& mem, uintptr_t base, uint32_t widthOffset, uint32_t heightOffset, int& width, int& height) {
    if (!mem.IsValidPtr(base)) return false;

    const int candidateW = mem.Read<int>(base + widthOffset);
    const int candidateH = mem.Read<int>(base + heightOffset);
    if (!IsPlausibleResolution(candidateW, candidateH)) return false;

    width = candidateW;
    height = candidateH;
    return true;
}

bool Memory::DetectGameResolution() {
    int detectedW = 0;
    int detectedH = 0;
    std::string source = "fallback";

    if (settings::overlayAutoResolution && g_dma.IsReady() && BaseAddress && ProcessID) {
        const uintptr_t cGame = Read<uintptr_t>(BaseAddress + offsets::cgame_offset);
        if (IsValidPtr(cGame)) {
            static const uint32_t cgamePairs[][2] = {
                { 0x0108, 0x010C }, { 0x0110, 0x0114 }, { 0x01C0, 0x01C4 },
                { 0x04F0, 0x04F4 }, { 0x0500, 0x0504 }, { 0x0578, 0x057C },
            };

            for (const auto& pair : cgamePairs) {
                if (TryReadResolutionPair(*this, cGame, pair[0], pair[1], detectedW, detectedH)) {
                    source = "cgame";
                    break;
                }
            }

            if (!detectedW) {
                const uintptr_t cam = Read<uintptr_t>(cGame + offsets::cgame::camera);
                static const uint32_t cameraPairs[][2] = {
                    { 0x02E8, 0x02EC }, { 0x02F0, 0x02F4 }, { 0x01A0, 0x01A4 },
                };
                for (const auto& pair : cameraPairs) {
                    if (TryReadResolutionPair(*this, cam, pair[0], pair[1], detectedW, detectedH)) {
                        source = "camera";
                        break;
                    }
                }
            }
        }
    }

    if (!detectedW) {
        detectedW = settings::overlayWidth > 0 ? settings::overlayWidth : GetSystemMetrics(SM_CXSCREEN);
        detectedH = settings::overlayHeight > 0 ? settings::overlayHeight : GetSystemMetrics(SM_CYSCREEN);
        source = settings::overlayAutoResolution ? "fallback" : "manual";
    }

    GameScreenWidth = detectedW;
    GameScreenHeight = detectedH;
    resolutionSource = source;
    return detectedW > 0 && detectedH > 0;
}

bool Memory::UpdateGameWindow() {
    static uint64_t lastResolutionCheck = 0;
    const uint64_t now = GetTickCount64();
    if (now - lastResolutionCheck >= 2000) {
        DetectGameResolution();
        lastResolutionCheck = now;
    }

    int targetW = GameScreenWidth > 0 ? GameScreenWidth : GetSystemMetrics(SM_CXSCREEN);
    int targetH = GameScreenHeight > 0 ? GameScreenHeight : GetSystemMetrics(SM_CYSCREEN);
    int targetX = settings::overlayX;
    int targetY = settings::overlayY;

    if (targetW < 100) targetW = GetSystemMetrics(SM_CXSCREEN);
    if (targetH < 100) targetH = GetSystemMetrics(SM_CYSCREEN);

    const int prevW = ScreenWidth;
    const int prevH = ScreenHeight;

    const bool boundsChanged =
        targetX != LastRect.left || targetY != LastRect.top ||
        targetW != (LastRect.right - LastRect.left) || targetH != (LastRect.bottom - LastRect.top);

    ScreenWidth = targetW;
    ScreenHeight = targetH;

    LastRect.left = targetX;
    LastRect.top = targetY;
    LastRect.right = targetX + targetW;
    LastRect.bottom = targetY + targetH;

    if (g_hwndOverlay && (boundsChanged || bShowMenu != LastMenuState)) {
        UINT flags = SWP_SHOWWINDOW;
        if (!bShowMenu) flags |= SWP_NOACTIVATE;
        SetWindowPos(g_hwndOverlay, HWND_TOPMOST, targetX, targetY, targetW, targetH, flags);
        LastMenuState = bShowMenu;
    }

    if (g_hwndOverlay && (targetW != prevW || targetH != prevH)) {
        ResizeOverlayRenderTargets(targetW, targetH);
    }

    return true;
}