#include "memory.hpp"
#include "config.hpp"
#include <iostream>
#include <cstring>

VmmdllApi g_vmm;

bool Memory::Connect() {
    if (g_vmm.handle) return true;
    if (!g_vmm.dll && !g_vmm.Load()) {
        std::cout << "[!] Failed to load vmm.dll - place MemProcFS files next to the executable." << std::endl;
        return false;
    }

    std::string device = settings::dmaDevice.empty() ? "fpga" : settings::dmaDevice;
    std::string deviceArg = "-device";
    std::string deviceVal = device;
    std::string norefresh = "-norefresh";
    std::string waitInit = "-waitinitialize";

    LPCSTR args[] = { "", deviceArg.c_str(), deviceVal.c_str(), norefresh.c_str(), waitInit.c_str() };
    g_vmm.handle = g_vmm.Initialize(5, args);
    if (!g_vmm.handle) {
        std::cout << "[!] VMMDLL_Initialize failed. Check DMA device (" << device << ")." << std::endl;
        return false;
    }

    std::cout << " [+] DMA connected via device: " << device << std::endl;
    return true;
}

void Memory::Disconnect() {
    g_vmm.Unload();
    ProcessID = 0;
    BaseAddress = 0;
}

bool Memory::ReadBuffer(uintptr_t addr, void* buffer, size_t size) {
    if (!g_vmm.handle || !ProcessID || !addr || !buffer || size == 0) return false;

    DWORD bytesRead = 0;
    return g_vmm.MemReadEx(g_vmm.handle, ProcessID, addr, reinterpret_cast<PBYTE>(buffer), static_cast<DWORD>(size), &bytesRead, VMMDLL_FLAG_NOCACHE) && bytesRead == size;
}

std::string Memory::ReadString(uintptr_t addr, size_t maxLen) {
    std::vector<char> buf(maxLen);
    ReadBuffer(addr, buf.data(), maxLen);
    buf[maxLen - 1] = 0;
    return std::string(buf.data());
}

DWORD Memory::GetPID(const std::wstring& procName) {
    if (!g_vmm.handle) return 0;

    std::string narrow(procName.begin(), procName.end());
    DWORD pid = 0;
    if (g_vmm.PidGetFromName(g_vmm.handle, narrow.c_str(), &pid)) {
        return pid;
    }
    return 0;
}

bool Memory::Attach(const std::string& procName) {
    ProcessID = GetPID(std::wstring(procName.begin(), procName.end()));
    if (ProcessID == 0) return false;

    BaseAddress = static_cast<uintptr_t>(g_vmm.ProcessGetModuleBaseU(g_vmm.handle, ProcessID, procName.c_str()));
    return BaseAddress != 0;
}

bool Memory::UpdateOffsets() {
    return offsets::LoadFromApi();
}

static bool IsOwnProcessWindow(HWND hwnd) {
    if (!hwnd) return true;
    if (hwnd == g_hwndOverlay) return true;

    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == GetCurrentProcessId()) return true;

    char className[64] = {};
    GetClassNameA(hwnd, className, sizeof(className));
    if (strcmp(className, "ConsoleWindowClass") == 0) return true;
    if (strcmp(className, "SaturnClass") == 0) return true;

    return false;
}

static bool IsUsableTargetWindow(HWND hwnd) {
    if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
    if (IsOwnProcessWindow(hwnd)) return false;

    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return false;
    return (r.right - r.left) >= 320 && (r.bottom - r.top) >= 240;
}

static long long WindowArea(HWND hwnd) {
    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return 0;
    return static_cast<long long>(r.right - r.left) * static_cast<long long>(r.bottom - r.top);
}

static bool GetWindowBounds(HWND hwnd, bool useClientRect, int& x, int& y, int& w, int& h) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    RECT r{};
    if (useClientRect) {
        if (!GetClientRect(hwnd, &r)) return false;
        POINT pt{ 0, 0 };
        ClientToScreen(hwnd, &pt);
        x = pt.x;
        y = pt.y;
        w = r.right - r.left;
        h = r.bottom - r.top;
    }
    else {
        if (!GetWindowRect(hwnd, &r)) return false;
        x = r.left;
        y = r.top;
        w = r.right - r.left;
        h = r.bottom - r.top;
    }

    return w > 0 && h > 0;
}

static HWND FindBestWindowByTitleSubstring(const std::string& title) {
    if (title.empty()) return NULL;

    struct SearchData {
        std::string needle;
        HWND best;
        long long bestArea;
    } data{ title, NULL, 0 };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* search = reinterpret_cast<SearchData*>(lParam);
        if (!IsUsableTargetWindow(hwnd)) return TRUE;

        char windowTitle[256] = {};
        GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
        if (windowTitle[0] == '\0') return TRUE;
        if (strstr(windowTitle, search->needle.c_str()) == nullptr) return TRUE;

        long long area = WindowArea(hwnd);
        if (area > search->bestArea) {
            search->bestArea = area;
            search->best = hwnd;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return data.best;
}

static const char* kAutoCaptureTitles[] = {
    "OBS",
    "预览",
    "采集",
    "Preview",
    "PotPlayer",
    "elgato",
    "AVerMedia",
    "4K Video Capture",
    "Live Gamer",
    "Streamlabs",
    "NVIDIA Share"
};

static HWND FindAutoCaptureWindow() {
    HWND best = NULL;
    long long bestArea = 0;

    for (const char* title : kAutoCaptureTitles) {
        HWND candidate = FindBestWindowByTitleSubstring(title);
        long long area = WindowArea(candidate);
        if (candidate && area > bestArea) {
            best = candidate;
            bestArea = area;
        }
    }

    return best;
}

static std::string GetWindowTitle(HWND hwnd) {
    char windowTitle[256] = {};
    GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
    return std::string(windowTitle);
}

void LogVisibleCaptureCandidates() {
    std::cout << "[>] Visible windows on this machine (use a unique substring in capture_window):" << std::endl;

    EnumWindows([](HWND hwnd, LPARAM) -> BOOL {
        if (!IsUsableTargetWindow(hwnd)) return TRUE;

        char windowTitle[256] = {};
        GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
        if (windowTitle[0] == '\0') return TRUE;

        RECT r{};
        GetWindowRect(hwnd, &r);
        std::cout << "     - \"" << windowTitle << "\" (" << (r.right - r.left) << "x" << (r.bottom - r.top) << ")" << std::endl;
        return TRUE;
    }, 0);
}

bool Memory::UpdateGameWindow() {
    const bool manualMode = settings::overlayAlignMode == "manual";
    int targetW = settings::overlayWidth > 0 ? settings::overlayWidth : GetSystemMetrics(SM_CXSCREEN);
    int targetH = settings::overlayHeight > 0 ? settings::overlayHeight : GetSystemMetrics(SM_CYSCREEN);
    int targetX = settings::overlayX;
    int targetY = settings::overlayY;
    overlayAlignSource = manualMode ? "manual" : "unset";

    if (!manualMode) {
        HWND targetHwnd = NULL;

        if (!settings::captureWindowTitle.empty()) {
            targetHwnd = FindBestWindowByTitleSubstring(settings::captureWindowTitle);
            if (targetHwnd) overlayAlignSource = "capture:" + settings::captureWindowTitle;
        }
        else if (settings::overlayAutoCapture) {
            targetHwnd = FindAutoCaptureWindow();
            if (targetHwnd) overlayAlignSource = "auto:" + GetWindowTitle(targetHwnd);
        }

        if (targetHwnd && IsUsableTargetWindow(targetHwnd)) {
            GameHwnd = targetHwnd;
            int x = 0, y = 0, w = 0, h = 0;
            if (GetWindowBounds(GameHwnd, settings::overlayUseClientRect, x, y, w, h)) {
                targetX = x;
                targetY = y;
                targetW = w;
                targetH = h;
            }
        }
        else {
            GameHwnd = NULL;
            if (!settings::captureWindowTitle.empty()) {
                overlayAlignSource = "missing:" + settings::captureWindowTitle;
            }
            else if (settings::overlayAutoCapture) {
                overlayAlignSource = "auto:not_found";
            }

            targetX = settings::overlayX;
            targetY = settings::overlayY;
            if (settings::overlayWidth > 0) targetW = settings::overlayWidth;
            if (settings::overlayHeight > 0) targetH = settings::overlayHeight;
        }
    }
    else {
        GameHwnd = NULL;
    }

    const int prevW = ScreenWidth;
    const int prevH = ScreenHeight;
    ScreenWidth = targetW;
    ScreenHeight = targetH;

    const bool boundsChanged =
        targetX != LastRect.left || targetY != LastRect.top ||
        targetW != (LastRect.right - LastRect.left) || targetH != (LastRect.bottom - LastRect.top);

    LastRect.left = targetX;
    LastRect.top = targetY;
    LastRect.right = targetX + targetW;
    LastRect.bottom = targetY + targetH;

    if (g_hwndOverlay && (boundsChanged || bShowMenu != LastMenuState)) {
        UINT flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
        HWND insertAfter = bShowMenu ? HWND_NOTOPMOST : HWND_TOPMOST;
        SetWindowPos(g_hwndOverlay, insertAfter, targetX, targetY, targetW, targetH, flags);
        LastMenuState = bShowMenu;
    }

    if (g_hwndOverlay && (targetW != prevW || targetH != prevH)) {
        ResizeOverlayRenderTargets(targetW, targetH);
    }

    return true;
}