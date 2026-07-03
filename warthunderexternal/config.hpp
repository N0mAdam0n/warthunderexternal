#pragma once
#include <string>

namespace settings {
    extern std::string dmaFolder;
    extern std::string dmaDevice;
    extern bool dmaDisableRefresh;
    extern std::string targetProcess;
    extern int overlayAlignMode;
    extern std::string captureWindowTitle;
    extern bool overlayUseClientRect;
    extern int overlayX;
    extern int overlayY;
    extern int overlayWidth;
    extern int overlayHeight;

    extern bool bUseKmbox;
    extern std::string kmboxIp;
    extern std::string kmboxPort;
    extern std::string kmboxUuid;
}

bool LoadDmaConfig(const char* path = "dma_config.ini");