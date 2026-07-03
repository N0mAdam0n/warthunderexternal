#include "memory.hpp"
#include "config.hpp"
#include <iostream>
#include <cstring>

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

static HWND FindWindowByTitleSubstring(const std::string& title) {
    if (title.empty()) return NULL;

    struct SearchData {
        std::string needle;
        HWND result;
    } data{ title, NULL };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        auto* search = reinterpret_cast<SearchData*>(lParam);
        char windowTitle[256] = {};
        GetWindowTextA(hwnd, windowTitle, sizeof(windowTitle));
        if (windowTitle[0] == '\0') return TRUE;
        if (strstr(windowTitle, search->needle.c_str()) != nullptr) {
            search->result = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return data.result;
}

bool Memory::UpdateGameWindow() {
    int targetW = settings::overlayWidth > 0 ? settings::overlayWidth : GetSystemMetrics(SM_CXSCREEN);
    int targetH = settings::overlayHeight > 0 ? settings::overlayHeight : GetSystemMetrics(SM_CYSCREEN);
    int targetX = settings::overlayX;
    int targetY = settings::overlayY;

    if (!settings::captureWindowTitle.empty()) {
        if (!GameHwnd || !IsWindow(GameHwnd)) {
            GameHwnd = FindWindowByTitleSubstring(settings::captureWindowTitle);
        }
        if (GameHwnd && IsWindow(GameHwnd)) {
            RECT r{};
            if (GetClientRect(GameHwnd, &r)) {
                POINT pt{ 0, 0 };
                ClientToScreen(GameHwnd, &pt);
                targetX = pt.x;
                targetY = pt.y;
                targetW = r.right - r.left;
                targetH = r.bottom - r.top;
            }
        }
    }
    else {
        GameHwnd = NULL;
    }

    ScreenWidth = targetW;
    ScreenHeight = targetH;

    if (g_hwndOverlay && (targetX != LastRect.left || targetY != LastRect.top ||
        targetW != (LastRect.right - LastRect.left) || targetH != (LastRect.bottom - LastRect.top) ||
        bShowMenu != LastMenuState)) {
        UINT flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
        HWND insertAfter = bShowMenu ? HWND_NOTOPMOST : HWND_TOPMOST;
        SetWindowPos(g_hwndOverlay, insertAfter, targetX, targetY, targetW, targetH, flags);

        LastRect.left = targetX;
        LastRect.top = targetY;
        LastRect.right = targetX + targetW;
        LastRect.bottom = targetY + targetH;
        LastMenuState = bShowMenu;
    }

    return true;
}