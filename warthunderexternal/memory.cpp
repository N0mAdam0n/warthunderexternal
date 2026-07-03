#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "memory.hpp"
#include "config.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <vector>

std::string g_overlayAlignSource = "manual";

static std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &out[0], size, nullptr, nullptr);
    return out;
}

static std::string GetWindowTitleUtf8(HWND hwnd) {
    if (!hwnd) return {};
    wchar_t title[512]{};
    GetWindowTextW(hwnd, title, 512);
    return WideToUtf8(title);
}

static bool ContainsInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    auto toLower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
    std::string h = haystack;
    std::string n = needle;
    std::transform(h.begin(), h.end(), h.begin(), toLower);
    std::transform(n.begin(), n.end(), n.begin(), toLower);
    return h.find(n) != std::string::npos;
}

static bool IsUsableTargetWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
    if (hwnd == g_hwndOverlay) return false;

    wchar_t className[128]{};
    GetClassNameW(hwnd, className, 128);
    if (wcscmp(className, L"SaturnClass") == 0) return false;

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return false;
    return (rect.right - rect.left) > 100 && (rect.bottom - rect.top) > 100;
}

static bool GetTargetBounds(HWND hwnd, RECT& out) {
    if (!hwnd) return false;

    if (settings::overlayUseClientRect) {
        RECT client{};
        if (!GetClientRect(hwnd, &client)) return false;
        POINT topLeft{ 0, 0 };
        ClientToScreen(hwnd, &topLeft);
        out.left = topLeft.x;
        out.top = topLeft.y;
        out.right = topLeft.x + (client.right - client.left);
        out.bottom = topLeft.y + (client.bottom - client.top);
        return (out.right > out.left) && (out.bottom > out.top);
    }

    return GetWindowRect(hwnd, &out) == TRUE;
}

static HWND FindWindowByTitleSubstring(const std::string& title) {
    if (title.empty()) return NULL;

    struct SearchData {
        std::string needle;
        HWND result;
    } data{ title, NULL };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* search = reinterpret_cast<SearchData*>(lParam);
        if (!IsUsableTargetWindow(hwnd)) return TRUE;

        std::string windowTitle = GetWindowTitleUtf8(hwnd);
        if (windowTitle.empty()) return TRUE;
        if (ContainsInsensitive(windowTitle, search->needle)) {
            search->result = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return data.result;
}

static HWND ResolveTargetWindow() {
    const int mode = settings::overlayAlignMode;

    if (mode == 0) {
        g_overlayAlignSource = "manual";
        return NULL;
    }

    if (mode == 1) {
        HWND found = FindWindowByTitleSubstring(settings::captureWindowTitle);
        if (found) {
            g_overlayAlignSource = "capture: " + GetWindowTitleUtf8(found);
            return found;
        }
        g_overlayAlignSource = "capture: not found";
        return NULL;
    }

    HWND fg = GetForegroundWindow();
    if (IsUsableTargetWindow(fg)) {
        g_overlayAlignSource = "foreground: " + GetWindowTitleUtf8(fg);
        return fg;
    }

    if (!settings::captureWindowTitle.empty()) {
        HWND found = FindWindowByTitleSubstring(settings::captureWindowTitle);
        if (found) {
            g_overlayAlignSource = "fallback capture: " + GetWindowTitleUtf8(found);
            return found;
        }
    }

    g_overlayAlignSource = "foreground: unavailable";
    return NULL;
}

bool Memory::QueryOverlayBounds(int& x, int& y, int& w, int& h) {
    x = settings::overlayX;
    y = settings::overlayY;
    w = settings::overlayWidth > 0 ? settings::overlayWidth : GetSystemMetrics(SM_CXSCREEN);
    h = settings::overlayHeight > 0 ? settings::overlayHeight : GetSystemMetrics(SM_CYSCREEN);

    HWND target = ResolveTargetWindow();
    GameHwnd = target;

    if (target) {
        RECT bounds{};
        if (GetTargetBounds(target, bounds)) {
            x = bounds.left;
            y = bounds.top;
            w = bounds.right - bounds.left;
            h = bounds.bottom - bounds.top;
            return true;
        }
    }

    return settings::overlayAlignMode == 0;
}

bool Memory::Connect() {
    if (g_dma.IsReady()) return true;

    if (!g_dma.LoadLibraries(settings::dmaFolder)) {
        std::cout << "[!] " << g_dma.lastError << std::endl;
        return false;
    }

    if (!g_dma.Initialize(settings::dmaDevice, settings::dmaDisableRefresh)) {
        std::cout << "[!] " << g_dma.lastError << std::endl;
        return false;
    }

    return true;
}

void Memory::Disconnect() {
    g_dma.Shutdown();
    ProcessID = 0;
    BaseAddress = 0;
}

bool Memory::ReadBuffer(uintptr_t addr, void* buffer, size_t size) {
    if (!g_dma.IsReady() || !ProcessID || !addr || !buffer || size == 0) return false;
    return g_dma.Read(ProcessID, addr, buffer, size);
}

std::string Memory::ReadString(uintptr_t addr, size_t maxLen) {
    std::vector<char> buf(maxLen);
    if (!ReadBuffer(addr, buf.data(), maxLen)) return {};
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
    if (BaseAddress == 0) return false;

    std::cout << " [+] Attached to " << procName << " (PID: " << ProcessID << ", Base: 0x"
        << std::hex << BaseAddress << std::dec << ")" << std::endl;
    return true;
}

bool Memory::UpdateOffsets() {
    return offsets::LoadFromApi();
}

bool Memory::UpdateGameWindow() {
    int targetX = 0;
    int targetY = 0;
    int targetW = 0;
    int targetH = 0;
    QueryOverlayBounds(targetX, targetY, targetW, targetH);

    if (targetW <= 0 || targetH <= 0) return false;

    static int lastRenderW = 0;
    static int lastRenderH = 0;

    ScreenWidth = targetW;
    ScreenHeight = targetH;

    if (!g_hwndOverlay) return true;

    const bool moved = (targetX != LastRect.left || targetY != LastRect.top ||
        targetW != (LastRect.right - LastRect.left) || targetH != (LastRect.bottom - LastRect.top));
    const bool menuChanged = bShowMenu != LastMenuState;
    const bool renderResized = targetW != lastRenderW || targetH != lastRenderH;

    if (moved || menuChanged) {
        UINT flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
        HWND insertAfter = bShowMenu ? HWND_NOTOPMOST : HWND_TOPMOST;
        SetWindowPos(g_hwndOverlay, insertAfter, targetX, targetY, targetW, targetH, flags);

        LastRect.left = targetX;
        LastRect.top = targetY;
        LastRect.right = targetX + targetW;
        LastRect.bottom = targetY + targetH;
        LastMenuState = bShowMenu;
    }

    if (renderResized) {
        ResizeOverlayRenderTargets(targetW, targetH);
        lastRenderW = targetW;
        lastRenderH = targetH;
    }

    return true;
}