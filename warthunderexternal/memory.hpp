#pragma once
#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include "offsets.hpp"

extern int ScreenWidth;
extern int ScreenHeight;
extern HWND g_hwndOverlay;
extern bool bShowMenu;

class Memory {
public:
    DWORD ProcessID = 0;
    uintptr_t BaseAddress = 0;
    HWND GameHwnd = NULL;

    RECT LastRect = { 0 };
    bool LastMenuState = false;

    bool Connect();
    void Disconnect();

    template <typename T> T Read(uintptr_t addr);
    template <typename T> bool Write(uintptr_t addr, const T& val);
    bool ReadBuffer(uintptr_t addr, void* buffer, size_t size);
    std::string ReadString(uintptr_t addr, size_t maxLen = 128);

    bool IsValidPtr(uintptr_t ptr);
    DWORD GetPID(const std::wstring& procName);
    bool Attach(const std::string& procName);
    bool UpdateOffsets();
    bool UpdateGameWindow();

    std::string overlayAlignSource;
};

void ResizeOverlayRenderTargets(int width, int height);
void LogVisibleCaptureCandidates();

extern Memory mem;

#include "dma/vmmdll_api.hpp"

extern VmmdllApi g_vmm;

inline bool Memory::IsValidPtr(uintptr_t ptr) {
    return (ptr >= 0x10000ULL && ptr < 0x00007FFFFFFFFFFFULL);
}

template <typename T>
T Memory::Read(uintptr_t addr) {
    T buffer = {};
    if (!g_vmm.handle || !ProcessID || !addr) return buffer;

    DWORD bytesRead = 0;
    g_vmm.MemReadEx(g_vmm.handle, ProcessID, addr, reinterpret_cast<PBYTE>(&buffer), sizeof(T), &bytesRead, VMMDLL_FLAG_NOCACHE);
    return buffer;
}

template <typename T>
bool Memory::Write(uintptr_t addr, const T& val) {
    if (!g_vmm.handle || !ProcessID || !addr) return false;
    return g_vmm.MemWrite(g_vmm.handle, ProcessID, addr, reinterpret_cast<PBYTE>(const_cast<T*>(&val)), sizeof(T));
}