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