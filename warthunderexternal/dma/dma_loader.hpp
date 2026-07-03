#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdint>
#include <string>

using VMM_HANDLE = void*;

using PFN_VMMDLL_Initialize = VMM_HANDLE(__cdecl*)(DWORD argc, LPCSTR argv[]);
using PFN_VMMDLL_InitializeEx = VMM_HANDLE(__cdecl*)(DWORD argc, LPCSTR argv[], void** ppLcErrorInfo);
using PFN_VMMDLL_Close = void(__cdecl*)(VMM_HANDLE hVMM);
using PFN_VMMDLL_ConfigSet = BOOL(__cdecl*)(VMM_HANDLE hVMM, ULONG64 fOption, ULONG64 qwValue);
using PFN_VMMDLL_PidGetFromName = BOOL(__cdecl*)(VMM_HANDLE hVMM, LPCSTR szProcName, PDWORD pdwPID);
using PFN_VMMDLL_ProcessGetModuleBaseU = ULONG64(__cdecl*)(VMM_HANDLE hVMM, DWORD dwPID, LPCSTR uszModuleName);
using PFN_VMMDLL_MemReadEx = BOOL(__cdecl*)(VMM_HANDLE hVMM, DWORD dwPID, ULONG64 qwA, PBYTE pb, DWORD cb, PDWORD pcbRead, ULONG64 flags);
using PFN_VMMDLL_MemWrite = BOOL(__cdecl*)(VMM_HANDLE hVMM, DWORD dwPID, ULONG64 qwA, PBYTE pb, DWORD cb);

constexpr ULONG64 VMMDLL_FLAG_NOCACHE = 0x0001;
constexpr ULONG64 VMMDLL_OPT_REFRESH_ALL = 0x2001ffff00000000ULL;

class DmaLoader {
public:
    VMM_HANDLE handle = nullptr;
    std::wstring dllDirectoryW;
    std::string dllDirectory;
    std::string lastError;

    bool LoadLibraries(const std::string& relativeFolder = "dma");
    bool Initialize(const std::string& device, bool disableRefresh = true);
    void Shutdown();

    bool RefreshAll();
    DWORD PidFromName(const std::string& processName);
    ULONG64 ModuleBase(DWORD pid, const std::string& moduleName);
    bool Read(DWORD pid, ULONG64 address, void* buffer, size_t size);
    bool Write(DWORD pid, ULONG64 address, const void* buffer, size_t size);

    bool IsReady() const { return handle != nullptr; }

private:
    HMODULE ftd3xxModule = nullptr;
    HMODULE leechcoreModule = nullptr;
    HMODULE leechcoreDriverModule = nullptr;
    HMODULE vmmModule = nullptr;

    PFN_VMMDLL_Initialize pfnInitialize = nullptr;
    PFN_VMMDLL_InitializeEx pfnInitializeEx = nullptr;
    PFN_VMMDLL_Close pfnClose = nullptr;
    PFN_VMMDLL_ConfigSet pfnConfigSet = nullptr;
    PFN_VMMDLL_PidGetFromName pfnPidGetFromName = nullptr;
    PFN_VMMDLL_ProcessGetModuleBaseU pfnProcessGetModuleBaseU = nullptr;
    PFN_VMMDLL_MemReadEx pfnMemReadEx = nullptr;
    PFN_VMMDLL_MemWrite pfnMemWrite = nullptr;

    std::wstring ResolveDllPath(const std::wstring& fileName) const;
    HMODULE LoadDependency(const wchar_t* fileName);
    bool VerifyDependencies();
    bool ResolveExports();
};

extern DmaLoader g_dma;