#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include "offsets.hpp"
#include "dma/dma_loader.hpp"

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
    bool QueryOverlayBounds(int& x, int& y, int& w, int& h);
};

extern Memory mem;
extern void ResizeOverlayRenderTargets(int width, int height);
extern std::string g_overlayAlignSource;

inline bool Memory::IsValidPtr(uintptr_t ptr) {
    return (ptr >= 0x10000ULL && ptr < 0x00007FFFFFFFFFFFULL);
}

template <typename T>
T Memory::Read(uintptr_t addr) {
    T buffer = {};
    if (!g_dma.IsReady() || !ProcessID || !addr) return buffer;
    g_dma.Read(ProcessID, addr, &buffer, sizeof(T));
    return buffer;
}

template <typename T>
bool Memory::Write(uintptr_t addr, const T& val) {
    if (!g_dma.IsReady() || !ProcessID || !addr) return false;
    return g_dma.Write(ProcessID, addr, &val, sizeof(T));
}