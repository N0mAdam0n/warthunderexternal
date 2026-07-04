#include "config.hpp"
#include <fstream>
#include <algorithm>
#include <cctype>

namespace settings {
    std::string dmaFolder = "dma";
    std::string dmaDevice = "fpga";
    int overlayX = 0;
    int overlayY = 0;
    int overlayWidth = 0;
    int overlayHeight = 0;

    bool bUseKmbox = false;
    std::string kmboxIp = "192.168.2.188";
    std::string kmboxPort = "8808";
    std::string kmboxUuid = "00000000";
}

static std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) start++;
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) end--;
    return value.substr(start, end - start);
}

static void ApplyConfigValue(const std::string& section, const std::string& key, const std::string& rawValue) {
    std::string value = Trim(rawValue);

    if (section == "dma") {
        if (key == "folder" && !value.empty()) settings::dmaFolder = value;
        else if (key == "device" && !value.empty()) settings::dmaDevice = value;
        return;
    }

    if (section == "device" && key == "device") {
        if (!value.empty()) settings::dmaDevice = value;
        return;
    }

    if (section == "overlay") {
        if (key == "overlay_x" && !value.empty()) settings::overlayX = std::stoi(value);
        else if (key == "overlay_y" && !value.empty()) settings::overlayY = std::stoi(value);
        else if (key == "overlay_width" && !value.empty()) settings::overlayWidth = std::stoi(value);
        else if (key == "overlay_height" && !value.empty()) settings::overlayHeight = std::stoi(value);
        return;
    }

    if (section == "kmbox") {
        if (key == "enabled") settings::bUseKmbox = (value == "1" || value == "true" || value == "yes");
        else if (key == "ip" && !value.empty()) settings::kmboxIp = value;
        else if (key == "port" && !value.empty()) settings::kmboxPort = value;
        else if (key == "uuid" && !value.empty()) settings::kmboxUuid = value;
    }
}

bool LoadDmaConfig(const char* path) {
    std::ifstream file(path);
    if (!file.good()) return false;

    std::string line;
    std::string section;
    while (std::getline(file, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Trim(line.substr(0, eq));
        std::string value = Trim(line.substr(eq + 1));
        ApplyConfigValue(section, key, value);
    }

    return true;
}