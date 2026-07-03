#pragma once
#include <Windows.h>
#include <cstdint>

using VMM_HANDLE = void*;

using PFN_VMMDLL_Initialize = VMM_HANDLE(__cdecl*)(DWORD argc, LPCSTR argv[]);
using PFN_VMMDLL_Close = void(__cdecl*)(VMM_HANDLE hVMM);
using PFN_VMMDLL_PidGetFromName = BOOL(__cdecl*)(VMM_HANDLE hVMM, LPCSTR szProcName, PDWORD pdwPID);
using PFN_VMMDLL_ProcessGetModuleBaseU = ULONG64(__cdecl*)(VMM_HANDLE hVMM, DWORD dwPID, LPCSTR uszModuleName);
using PFN_VMMDLL_MemReadEx = BOOL(__cdecl*)(VMM_HANDLE hVMM, DWORD dwPID, ULONG64 qwA, PBYTE pb, DWORD cb, PDWORD pcbRead, ULONG64 flags);
using PFN_VMMDLL_MemWrite = BOOL(__cdecl*)(VMM_HANDLE hVMM, DWORD dwPID, ULONG64 qwA, PBYTE pb, DWORD cb);

constexpr ULONG64 VMMDLL_FLAG_NOCACHE = 0x0001;

struct VmmdllApi {
    HMODULE dll = nullptr;
    VMM_HANDLE handle = nullptr;

    PFN_VMMDLL_Initialize Initialize = nullptr;
    PFN_VMMDLL_Close Close = nullptr;
    PFN_VMMDLL_PidGetFromName PidGetFromName = nullptr;
    PFN_VMMDLL_ProcessGetModuleBaseU ProcessGetModuleBaseU = nullptr;
    PFN_VMMDLL_MemReadEx MemReadEx = nullptr;
    PFN_VMMDLL_MemWrite MemWrite = nullptr;

    bool Load() {
        dll = LoadLibraryA("vmm.dll");
        if (!dll) return false;

        Initialize = reinterpret_cast<PFN_VMMDLL_Initialize>(GetProcAddress(dll, "VMMDLL_Initialize"));
        Close = reinterpret_cast<PFN_VMMDLL_Close>(GetProcAddress(dll, "VMMDLL_Close"));
        PidGetFromName = reinterpret_cast<PFN_VMMDLL_PidGetFromName>(GetProcAddress(dll, "VMMDLL_PidGetFromName"));
        ProcessGetModuleBaseU = reinterpret_cast<PFN_VMMDLL_ProcessGetModuleBaseU>(GetProcAddress(dll, "VMMDLL_ProcessGetModuleBaseU"));
        MemReadEx = reinterpret_cast<PFN_VMMDLL_MemReadEx>(GetProcAddress(dll, "VMMDLL_MemReadEx"));
        MemWrite = reinterpret_cast<PFN_VMMDLL_MemWrite>(GetProcAddress(dll, "VMMDLL_MemWrite"));

        return Initialize && Close && PidGetFromName && ProcessGetModuleBaseU && MemReadEx && MemWrite;
    }

    void Unload() {
        if (handle && Close) {
            Close(handle);
            handle = nullptr;
        }
        if (dll) {
            FreeLibrary(dll);
            dll = nullptr;
        }
    }
};