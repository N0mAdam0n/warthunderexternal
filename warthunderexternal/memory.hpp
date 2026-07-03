#pragma once
#include <Windows.h>
#include <TlHelp32.h>
#include <iostream>
#include <string>
#include <vector>

// Forward declarations
extern int ScreenWidth;
extern int ScreenHeight;
extern HWND g_hwndOverlay;
extern bool bShowMenu;

#define IO_READ_REQUEST       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IO_WRITE_REQUEST      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IO_GET_MODULE_REQUEST CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

typedef struct _KERNEL_REQUEST { ULONG ProcessId; UINT64 Address; void* Buffer; SIZE_T Size; } KERNEL_REQUEST, * PKERNEL_REQUEST;

class Memory {
public:
    HANDLE hDriver = INVALID_HANDLE_VALUE;
    DWORD ProcessID = 0;
    uintptr_t BaseAddress = 0;
    HWND GameHwnd = NULL;

    // Anti-Flicker State
    RECT LastRect = { 0 };
    bool LastMenuState = false;

    bool Connect() {
        if (hDriver == INVALID_HANDLE_VALUE) hDriver = CreateFileA("\\\\.\\Nul", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
        return hDriver != INVALID_HANDLE_VALUE;
    }

    void Disconnect() {
        if (hDriver != INVALID_HANDLE_VALUE) {
            CloseHandle(hDriver);
            hDriver = INVALID_HANDLE_VALUE;
        }
    }

    template <typename T> T Read(uintptr_t addr) {
        T buffer = {};
        if (hDriver == INVALID_HANDLE_VALUE || !addr) return buffer;
        KERNEL_REQUEST req = { ProcessID, addr, &buffer, sizeof(T) };
        DeviceIoControl(hDriver, IO_READ_REQUEST, &req, sizeof(req), 0, 0, NULL, NULL);
        return buffer;
    }

    template <typename T> bool Write(uintptr_t addr, const T& val) {
        if (hDriver == INVALID_HANDLE_VALUE || !addr) return false;
        KERNEL_REQUEST req = { ProcessID, addr, (void*)&val, sizeof(T) };
        return DeviceIoControl(hDriver, IO_WRITE_REQUEST, &req, sizeof(req), 0, 0, NULL, NULL);
    }

    bool ReadBuffer(uintptr_t addr, void* buffer, size_t size) {
        if (hDriver == INVALID_HANDLE_VALUE || !addr) return false;
        KERNEL_REQUEST req = { ProcessID, addr, buffer, size };
        return DeviceIoControl(hDriver, IO_READ_REQUEST, &req, sizeof(req), 0, 0, NULL, NULL);
    }

    std::string ReadString(uintptr_t addr, size_t maxLen = 128) {
        std::vector<char> buf(maxLen);
        ReadBuffer(addr, buf.data(), maxLen);
        buf[maxLen - 1] = 0;
        return std::string(buf.data());
    }

    bool IsValidPtr(uintptr_t ptr) {
        return (ptr >= 0x10000ULL && ptr < 0x00007FFFFFFFFFFFULL);
    }

    DWORD GetPID(const std::wstring& procName) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(PROCESSENTRY32W);
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
        if (snapshot == INVALID_HANDLE_VALUE) return 0;
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (!_wcsicmp(entry.szExeFile, procName.c_str())) {
                    CloseHandle(snapshot);
                    return entry.th32ProcessID;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return 0;
    }

    bool Attach(const std::string& procName) {
        std::wstring wName(procName.begin(), procName.end());
        ProcessID = GetPID(wName);
        if (ProcessID == 0) return false;

        uintptr_t baseBuffer = 0;
        KERNEL_REQUEST req = { ProcessID, 0, &baseBuffer, sizeof(uintptr_t) };
        if (DeviceIoControl(hDriver, IO_GET_MODULE_REQUEST, &req, sizeof(req), 0, 0, NULL, NULL) && baseBuffer != 0) {
            BaseAddress = baseBuffer;
            return true;
        }
        return false;
    }

    bool UpdateOffsets() {
        return true; // Using hardcoded offsets now
    }

    bool UpdateGameWindow() {
        if (!GameHwnd || !IsWindow(GameHwnd)) {
            GameHwnd = FindWindowA("DagorWClass", NULL);
            if (!GameHwnd) return false;
        }
        RECT r;
        if (GetClientRect(GameHwnd, &r)) {
            POINT pt = { 0, 0 };
            ClientToScreen(GameHwnd, &pt);

            // Update Globals
            ScreenWidth = r.right - r.left;
            ScreenHeight = r.bottom - r.top;

            if (g_hwndOverlay && (pt.x != LastRect.left || pt.y != LastRect.top || ScreenWidth != (LastRect.right - LastRect.left) || ScreenHeight != (LastRect.bottom - LastRect.top) || bShowMenu != LastMenuState)) {

                UINT flags = SWP_NOACTIVATE;
                if (bShowMenu)
                    SetWindowPos(g_hwndOverlay, HWND_NOTOPMOST, pt.x, pt.y, ScreenWidth, ScreenHeight, flags);
                else
                    SetWindowPos(g_hwndOverlay, HWND_TOPMOST, pt.x, pt.y, ScreenWidth, ScreenHeight, flags);

                // Update Cache
                LastRect.left = pt.x; LastRect.top = pt.y;
                LastRect.right = pt.x + ScreenWidth; LastRect.bottom = pt.y + ScreenHeight;
                LastMenuState = bShowMenu;
            }
            return true;
        }
        return false;
    }
};

extern Memory mem;