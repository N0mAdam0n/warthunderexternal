#include "offsets.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <vector>
#include <cstring>

#pragma comment(lib, "winhttp.lib")

namespace offsets {

    static std::string HttpGet(const wchar_t* host, const wchar_t* path) {
        std::string result;
        HINTERNET hSession = WinHttpOpen(L"warthunderexternal/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
        if (!hSession) return result;

        HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return result; }

        if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
            WinHttpReceiveResponse(hRequest, nullptr)) {
            DWORD bytesAvailable = 0;
            do {
                if (!WinHttpQueryDataAvailable(hRequest, &bytesAvailable) || bytesAvailable == 0) break;
                std::vector<char> buffer(bytesAvailable);
                DWORD bytesRead = 0;
                if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
                    result.append(buffer.data(), bytesRead);
                }
            } while (bytesAvailable > 0);
        }

        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    static std::string ExtractJsonString(const std::string& json, const char* key) {
        std::string search = std::string("\"") + key + "\":\"";
        size_t pos = json.find(search);
        if (pos == std::string::npos) return "";
        pos += search.size();
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }

    static uintptr_t ParseHex(const std::string& value) {
        if (value.empty()) return 0;
        return static_cast<uintptr_t>(std::stoull(value, nullptr, 16));
    }

    static void ApplyApiValue(const std::string& name, uintptr_t value) {
        static const std::unordered_map<std::string, std::function<void(uintptr_t)>> map = {
            {"g_GameContext",              [](uintptr_t v) { cgame_offset = v; }},
            {"g_ViewMatrix",               [](uintptr_t v) { view_matrix_offset = v; }},
            {"g_HudInfo",                  [](uintptr_t v) { hud_offset = v; }},
            {"g_LocalPlayer",              [](uintptr_t v) { localplayer_offset = v; }},
            {"g_AllListData",              [](uintptr_t v) { alllistdata_offset = v; }},
            {"g_GameOptics",               [](uintptr_t v) { c_optics_offset = v; }},
            {"g_BombListIndexPtr",         [](uintptr_t v) { g_bombs_index = v; }},
            {"g_RocketListIndexPtr",       [](uintptr_t v) { g_rockets_index = v; }},
            {"g_ViewAngles",               [](uintptr_t v) { yaw_offset = v; yaw_offset_y = v + 4; }},

            {"m_BallisticsCont",           [](uintptr_t v) { cgame::ballistics = static_cast<uint32_t>(v); }},
            {"m_CameraCont",               [](uintptr_t v) { cgame::camera = static_cast<uint32_t>(v); }},
            {"m_UnitList3",                [](uintptr_t v) { cgame::unitlist = static_cast<uint32_t>(v); }},
            {"m_UnitCount3",               [](uintptr_t v) { cgame::unitcount = static_cast<uint32_t>(v); }},

            {"m_BombPredPos",              [](uintptr_t v) { ballistic::bomb_impact_point = static_cast<uint32_t>(v); }},

            {"m_ControlledUnit",           [](uintptr_t v) { localplayer::localunit_offset = static_cast<uint32_t>(v); }},

            {"m_Velocity",                 [](uintptr_t v) {
                // GroundMovement::m_Velocity and Unit::m_Velocity share the name; apply Unit first via ordering below
            }},
            {"m_AirContainer",             [](uintptr_t v) { unit::airmovement_offset = static_cast<uint32_t>(v); }},
            {"m_BBMin",                    [](uintptr_t v) { unit::bbmin_offset = static_cast<uint32_t>(v); }},
            {"m_BBMax",                    [](uintptr_t v) { unit::bbmax_offset = static_cast<uint32_t>(v); }},
            {"m_RotationMatrix",           [](uintptr_t v) { unit::rotation_matrix = static_cast<uint32_t>(v); }},
            {"m_Position",                 [](uintptr_t v) {
                // Multiple groups use m_Position; resolved by group in parser
            }},
            {"m_UnitInfo",                 [](uintptr_t v) { unit::info_offset = static_cast<uint32_t>(v); }},
            {"m_TeamNum",                  [](uintptr_t v) { unit::unitArmyNo_offset = static_cast<uint32_t>(v); }},
            {"m_UnitState",                [](uintptr_t v) { unit::unitState_offset = static_cast<uint32_t>(v); }},
            {"m_UserId",                   [](uintptr_t v) { unit::userId_offset = static_cast<uint32_t>(v); }},
            {"m_PlayerId",                 [](uintptr_t v) { unit::userId_offset = static_cast<uint32_t>(v); }},
            {"m_GaijinId",                 [](uintptr_t v) { unit::userId_offset = static_cast<uint32_t>(v); }},
            {"m_PlayerNick",               [](uintptr_t v) { wtinfo::PlayerNick = static_cast<uint32_t>(v); }},
            {"m_NickName",                 [](uintptr_t v) { wtinfo::PlayerNick = static_cast<uint32_t>(v); }},
            {"m_UserName",                 [](uintptr_t v) { wtinfo::PlayerNick = static_cast<uint32_t>(v); }},
            {"m_VisualReload",             [](uintptr_t v) { unit::visualReloadProgress_offset = static_cast<uint32_t>(v); }},
            {"m_DamageModelCont",          [](uintptr_t v) { damage_model::ptrs[0] = v; damage_model::count[0] = v; }},

            {"m_ArcadeCrosshair",          [](uintptr_t v) { hud::arcade_crosshair = static_cast<uint32_t>(v); }},
        };

        auto it = map.find(name);
        if (it != map.end()) it->second(value);
    }

    static void ApplyGroupedValue(const std::string& group, const std::string& name, uintptr_t value) {
        if (group == "Camera" && name == "m_ViewMatrix") {
            camera::matrix = static_cast<uint32_t>(value);
            return;
        }
        if (group == "GroundMovement" && name == "m_Velocity") {
            unit::ground_velocity_offset = static_cast<uint32_t>(value);
            return;
        }
        if (group == "Unit" && name == "m_Velocity") {
            unit::groundmovement_offset = static_cast<uint32_t>(value);
            return;
        }
        if (group == "Unit" && name == "m_Position") {
            unit::position_offset = static_cast<uint32_t>(value);
            return;
        }
        if (group == "Projectile" && name == "m_Position") {
            shell::position = value;
            return;
        }
        if (group == "Projectile" && name == "m_Velocity") {
            shell::velocity = value;
            return;
        }
        if (group == "Projectile" && name == "m_BombNameCont") {
            shell::bomb::name_offsets[0] = value;
            return;
        }
        if (group == "Projectile" && name == "m_MissileNameCont") {
            shell::rocket::name_offsets[0] = value;
            return;
        }
        if (group == "BombNameCont" && name == "m_Name") {
            shell::bomb::name_offsets[1] = value;
            return;
        }
        if (group == "MissileNameCont" && name == "m_Type") {
            shell::rocket::name_offsets[1] = value;
            return;
        }
        if (group == "InternalInfo" && name == "m_Position") {
            damage_model::dm_position = value;
            return;
        }

        ApplyApiValue(name, value);
    }

    bool LoadFromApi(const char* url) {
        const wchar_t* host = L"api.monkrel.cc";
        const wchar_t* path = L"/api/v1/offsets";

        if (url && strcmp(url, "https://api.monkrel.cc/api/v1/offsets") != 0) {
            std::cout << "[!] Custom offset URLs are not supported yet, using api.monkrel.cc" << std::endl;
        }

        std::string json = HttpGet(host, path);
        if (json.empty()) {
            std::cout << "[!] Failed to fetch offsets from API, using built-in defaults." << std::endl;
            return false;
        }

        api_version = ExtractJsonString(json, "version");
        api_channel = ExtractJsonString(json, "channel");

        size_t pos = 0;
        int applied = 0;
        while ((pos = json.find("\"group\":", pos)) != std::string::npos) {
            std::string group = ExtractJsonString(json.substr(pos), "group");
            std::string name = ExtractJsonString(json.substr(pos), "name");
            std::string value = ExtractJsonString(json.substr(pos), "value");

            if (!name.empty() && !value.empty()) {
                uintptr_t parsed = ParseHex(value);
                if (parsed != 0 || value == "0x0") {
                    ApplyGroupedValue(group, name, parsed);
                    applied++;
                }
            }
            pos += 8;
        }

        std::cout << " [+] Offsets updated from API";
        if (!api_version.empty()) std::cout << " (v" << api_version;
        if (!api_channel.empty()) std::cout << ", " << api_channel;
        if (!api_version.empty()) std::cout << ")";
        std::cout << " - " << applied << " entries applied." << std::endl;

        return applied > 0;
    }
}