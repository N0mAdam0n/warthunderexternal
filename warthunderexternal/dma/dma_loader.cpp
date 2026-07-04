#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "dma_loader.hpp"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <vector>

DmaLoader g_dma;

static std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &out[0], size, nullptr, nullptr);
    return out;
}

static std::wstring GetExeDirectoryW() {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exePath(modulePath);
    return exePath.parent_path().wstring();
}

std::wstring DmaLoader::ResolveDllPath(const std::wstring& fileName) const {
    return (std::filesystem::path(dllDirectoryW) / fileName).wstring();
}

HMODULE DmaLoader::LoadDependency(const wchar_t* fileName) {
    const std::wstring fullPath = ResolveDllPath(fileName);
    if (!std::filesystem::exists(fullPath)) {
        std::wcout << L"[!] Missing DMA dependency: " << fullPath << std::endl;
        return nullptr;
    }

    HMODULE module = LoadLibraryW(fullPath.c_str());
    if (!module) {
        std::wcout << L"[!] Failed to load: " << fullPath << L" (error " << GetLastError() << L")" << std::endl;
        return nullptr;
    }

    std::wcout << L" [+] Loaded " << fullPath << std::endl;
    return module;
}

bool DmaLoader::VerifyDependencies() {
    namespace fs = std::filesystem;
    const wchar_t* required[] = {
        L"FTD3XX.dll",
        L"leechcore.dll",
        L"vmm.dll",
    };

    bool missing = false;
    for (const wchar_t* file : required) {
        if (!fs::exists(ResolveDllPath(file))) {
            std::wcout << L"[!] Required file not found: " << ResolveDllPath(file) << std::endl;
            missing = true;
        }
    }

    if (fs::exists(ResolveDllPath(L"leechcore_driver.dll"))) {
        std::wcout << L" [i] Found leechcore_driver.dll" << std::endl;
    }

    if (missing) {
        lastError = "DMA dependencies are missing from the dma folder.";
        return false;
    }

    return true;
}

bool DmaLoader::LoadLibraries(const std::string& relativeFolder) {
    if (vmmModule) return true;

    const std::wstring exeDir = GetExeDirectoryW();
    dllDirectoryW = (std::filesystem::path(exeDir) / relativeFolder).wstring();
    dllDirectory = WideToUtf8(dllDirectoryW);

    if (!std::filesystem::exists(dllDirectoryW)) {
        lastError = "DMA folder not found: " + dllDirectory;
        std::cout << "[!] " << lastError << std::endl;
        std::cout << "     Copy MemProcFS DLLs into: " << dllDirectory << std::endl;
        return false;
    }

    if (!VerifyDependencies()) {
        return false;
    }

    if (!SetDllDirectoryW(dllDirectoryW.c_str())) {
        lastError = "Failed to set DLL search path to dma folder.";
        return false;
    }

    ftd3xxModule = LoadDependency(L"FTD3XX.dll");
    if (!ftd3xxModule) {
        lastError = "Failed to load FTD3XX.dll";
        return false;
    }

    leechcoreModule = LoadDependency(L"leechcore.dll");
    if (!leechcoreModule) {
        lastError = "Failed to load leechcore.dll";
        return false;
    }

    leechcoreDriverModule = LoadDependency(L"leechcore_driver.dll");

    vmmModule = LoadDependency(L"vmm.dll");
    if (!vmmModule) {
        lastError = "Failed to load vmm.dll";
        return false;
    }

    return ResolveExports();
}

bool DmaLoader::ResolveExports() {
    pfnInitialize = reinterpret_cast<PFN_VMMDLL_Initialize>(GetProcAddress(vmmModule, "VMMDLL_Initialize"));
    pfnInitializeEx = reinterpret_cast<PFN_VMMDLL_InitializeEx>(GetProcAddress(vmmModule, "VMMDLL_InitializeEx"));
    pfnClose = reinterpret_cast<PFN_VMMDLL_Close>(GetProcAddress(vmmModule, "VMMDLL_Close"));
    pfnConfigSet = reinterpret_cast<PFN_VMMDLL_ConfigSet>(GetProcAddress(vmmModule, "VMMDLL_ConfigSet"));
    pfnPidGetFromName = reinterpret_cast<PFN_VMMDLL_PidGetFromName>(GetProcAddress(vmmModule, "VMMDLL_PidGetFromName"));
    pfnProcessGetModuleBaseU = reinterpret_cast<PFN_VMMDLL_ProcessGetModuleBaseU>(GetProcAddress(vmmModule, "VMMDLL_ProcessGetModuleBaseU"));
    pfnMemReadEx = reinterpret_cast<PFN_VMMDLL_MemReadEx>(GetProcAddress(vmmModule, "VMMDLL_MemReadEx"));
    pfnMemWrite = reinterpret_cast<PFN_VMMDLL_MemWrite>(GetProcAddress(vmmModule, "VMMDLL_MemWrite"));

    if (!pfnInitialize || !pfnClose || !pfnPidGetFromName || !pfnProcessGetModuleBaseU || !pfnMemReadEx || !pfnMemWrite) {
        lastError = "vmm.dll is missing required exports.";
        return false;
    }

    return true;
}

bool DmaLoader::Initialize(const std::string& device, bool disableRefresh) {
    if (handle) return true;

    std::string deviceArg = "-device";
    std::string deviceVal = device.empty() ? "fpga" : device;
    std::string waitInit = "-waitinitialize";
    std::vector<std::string> argStorage = { "", deviceArg, deviceVal, waitInit };
    if (disableRefresh) {
        argStorage.emplace_back("-norefresh");
    }

    std::vector<LPCSTR> args;
    args.reserve(argStorage.size());
    for (const auto& item : argStorage) {
        args.push_back(item.c_str());
    }

    handle = pfnInitializeEx
        ? pfnInitializeEx(static_cast<DWORD>(args.size()), args.data(), nullptr)
        : pfnInitialize(static_cast<DWORD>(args.size()), args.data());

    if (!handle) {
        lastError = "VMMDLL_Initialize failed. Check FPGA device and cabling.";
        return false;
    }

    std::cout << " [+] MemProcFS initialized from: " << dllDirectory << std::endl;
    std::cout << "     Device: " << deviceVal << std::endl;
    return true;
}

void DmaLoader::Shutdown() {
    if (handle && pfnClose) {
        pfnClose(handle);
        handle = nullptr;
    }
    if (vmmModule) {
        FreeLibrary(vmmModule);
        vmmModule = nullptr;
    }
    if (leechcoreDriverModule) {
        FreeLibrary(leechcoreDriverModule);
        leechcoreDriverModule = nullptr;
    }
    if (leechcoreModule) {
        FreeLibrary(leechcoreModule);
        leechcoreModule = nullptr;
    }
    if (ftd3xxModule) {
        FreeLibrary(ftd3xxModule);
        ftd3xxModule = nullptr;
    }
    SetDllDirectoryW(nullptr);
}

bool DmaLoader::RefreshAll() {
    return handle && pfnConfigSet && pfnConfigSet(handle, VMMDLL_OPT_REFRESH_ALL, 1);
}

DWORD DmaLoader::PidFromName(const std::string& processName) {
    if (!handle || !pfnPidGetFromName) return 0;
    RefreshAll();
    DWORD pid = 0;
    return pfnPidGetFromName(handle, processName.c_str(), &pid) ? pid : 0;
}

ULONG64 DmaLoader::ModuleBase(DWORD pid, const std::string& moduleName) {
    if (!handle || !pfnProcessGetModuleBaseU || pid == 0) return 0;
    return pfnProcessGetModuleBaseU(handle, pid, moduleName.c_str());
}

bool DmaLoader::Read(DWORD pid, ULONG64 address, void* buffer, size_t size) {
    if (!handle || !pfnMemReadEx || pid == 0 || address == 0 || !buffer || size == 0) return false;

    auto* bytes = static_cast<uint8_t*>(buffer);
    size_t offset = 0;
    constexpr size_t maxChunk = 0x00100000;

    while (offset < size) {
        const DWORD chunk = static_cast<DWORD>((std::min)(size - offset, maxChunk));
        DWORD bytesRead = 0;
        if (!pfnMemReadEx(handle, pid, address + offset, bytes + offset, chunk, &bytesRead, VMMDLL_FLAG_NOCACHE) || bytesRead != chunk) {
            return false;
        }
        offset += chunk;
    }

    return true;
}

bool DmaLoader::Write(DWORD pid, ULONG64 address, const void* buffer, size_t size) {
    if (!handle || !pfnMemWrite || pid == 0 || address == 0 || !buffer || size == 0) return false;
    return pfnMemWrite(handle, pid, address, reinterpret_cast<PBYTE>(const_cast<void*>(buffer)), static_cast<DWORD>(size));
}