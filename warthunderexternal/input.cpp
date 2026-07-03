#include "input.hpp"
#include "config.hpp"
#include <Windows.h>
#include <iostream>

using PFN_kmNet_init = int(__cdecl*)(char* ip, char* port, char* mac);
using PFN_kmNet_mouse_move = int(__cdecl*)(short x, short y);

static HMODULE g_kmDll = nullptr;
static PFN_kmNet_init g_kmInit = nullptr;
static PFN_kmNet_mouse_move g_kmMove = nullptr;
static bool g_ready = false;
static bool g_warned = false;

bool Input::Init() {
    if (!settings::bUseKmbox) return false;
    if (g_ready) return true;

    g_kmDll = LoadLibraryA("kmNet.dll");
    if (!g_kmDll) {
        std::cout << "[!] kmNet.dll not found. Place Kmbox SDK DLL next to the executable." << std::endl;
        return false;
    }

    g_kmInit = reinterpret_cast<PFN_kmNet_init>(GetProcAddress(g_kmDll, "kmNet_init"));
    g_kmMove = reinterpret_cast<PFN_kmNet_mouse_move>(GetProcAddress(g_kmDll, "kmNet_mouse_move"));
    if (!g_kmInit || !g_kmMove) {
        std::cout << "[!] kmNet.dll is missing required exports." << std::endl;
        FreeLibrary(g_kmDll);
        g_kmDll = nullptr;
        return false;
    }

    char ip[64]{};
    char port[16]{};
    char uuid[64]{};
    strncpy_s(ip, settings::kmboxIp.c_str(), _TRUNCATE);
    strncpy_s(port, settings::kmboxPort.c_str(), _TRUNCATE);
    strncpy_s(uuid, settings::kmboxUuid.c_str(), _TRUNCATE);

    if (g_kmInit(ip, port, uuid) != 0) {
        std::cout << "[!] Kmbox init failed (" << settings::kmboxIp << ":" << settings::kmboxPort << ")." << std::endl;
        return false;
    }

    g_ready = true;
    std::cout << " [+] Kmbox connected (" << settings::kmboxIp << ":" << settings::kmboxPort << ")." << std::endl;
    return true;
}

void Input::Shutdown() {
    g_ready = false;
    g_kmInit = nullptr;
    g_kmMove = nullptr;
    if (g_kmDll) {
        FreeLibrary(g_kmDll);
        g_kmDll = nullptr;
    }
}

bool Input::IsReady() {
    return g_ready;
}

void Input::MoveMouseRelative(int dx, int dy) {
    if (!settings::bUseKmbox) {
        if (!g_warned) {
            std::cout << "[!] Aimbot requires Kmbox on DMA machine (enable in dma_config.ini)." << std::endl;
            g_warned = true;
        }
        return;
    }

    if (!g_ready && !Init()) return;
    g_kmMove(static_cast<short>(dx), static_cast<short>(dy));
}