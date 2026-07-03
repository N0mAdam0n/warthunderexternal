#pragma once
#include <cstdint>
#include <string>

namespace offsets {
    // Global pointers (defaults from api.monkrel.cc v2.57.0.39)
    inline uintptr_t cgame_offset = 0x706CA98;
    inline uintptr_t view_matrix_offset = 0x70C5F88;
    inline uintptr_t hud_offset = 0x706C0C8;
    inline uintptr_t localplayer_offset = 0x70473A0;
    inline uintptr_t alllistdata_offset = 0x73C5F68;
    inline uintptr_t c_optics_offset = 0x706E568;
    inline uintptr_t entity_manager_offset = 0x6e1cb60;
    inline uintptr_t game_time_offset = 0xffff80091c910000;

    inline uintptr_t g_bombs_index = 0x704A5E0;
    inline uintptr_t g_bullets_index = 0x6be9c80;
    inline uintptr_t g_rockets_index = 0x704BA98;
    inline uintptr_t yaw_offset = 0x7070EB8;
    inline uintptr_t yaw_offset_y = 0x7070EBC;

    namespace hud {
        inline uint32_t can_select_unit = 0x20;
        inline uint32_t arcade_crosshair = 0x2BC;
        inline uint32_t aircraft_distance = 0x2AA + 0x8;
        inline uint32_t ground_to_air_prediction = 0x2B0 + 0x8;
        inline uint32_t air_to_air_prediction = 0x29D + 0x8;
        inline uint32_t tank_esp = 0x1E4;
    }
    namespace cgame {
        inline uint32_t ballistics = 0x3F0;
        inline uint32_t camera = 0x670;
        inline uint32_t unitlist = 0x340;
        inline uint32_t unitcount = 0x350;
    }
    namespace camera {
        inline uint32_t matrix = 0x1D8;
    }
    namespace ballistic {
        inline uint32_t velocity = 0x2048;
        inline uint32_t bomb_impact_point = 0x1C94;
        inline uint32_t selected_unit_prediction = 0x6B0;
    }
    namespace localplayer {
        inline uint32_t localunit_offset = 0x918;
    }
    namespace unit {
        inline uint32_t groundmovement_offset = 0x2000;
        inline uint32_t ground_velocity_offset = 0x5C;
        inline uint32_t airmovement_offset = 0xD38;
        inline uint32_t air_velocity_offset = 0x15A8;
        inline uint32_t bbmin_offset = 0x250;
        inline uint32_t bbmax_offset = 0x25C;
        inline uint32_t rotation_matrix = 0xD04;
        inline uint32_t position_offset = 0xD28;
        inline uint32_t info_offset = 0x1010;
        inline uint32_t unitArmyNo_offset = 0x1000;
        inline uint32_t unitState_offset = 0xF80;
        inline uint32_t visualReloadProgress_offset = 0xAD8;
    }
    namespace wtinfo {
        inline uint32_t FullName = 0x0018;
    }
    namespace damage_model {
        inline uintptr_t ptrs[3] = { 0x1090, 0x58, 0xA0 };
        inline uintptr_t count[3] = { 0x1090, 0x58, 0xB0 };
        inline uintptr_t transform_info[2] = { 0x238, 0x0 };
        inline uintptr_t transform_matrix_offset = 0x40;
        inline uintptr_t next_damage_model = 0x8;
        inline uintptr_t transform_index = 0x4;
        inline uintptr_t forward = 0x10;
        inline uintptr_t up = 0x1C;
        inline uintptr_t left = 0x28;
        inline uintptr_t dm_position = 0x34;
        inline uintptr_t bbmin = 0x40;
        inline uintptr_t bbmax = 0x4C;
        inline uintptr_t vertices_ptr = 0x80;
        inline uintptr_t vertices_count = 0x88;
        inline uintptr_t indices_ptr = 0x90;
        inline uintptr_t indices_count = 0x98;
        inline uintptr_t dm_index = 0xBC;
        inline uintptr_t dm_name = 0xC0;
        inline uintptr_t struct_size = 208;
    }
    namespace shell {
        inline uintptr_t position = 0x298;
        inline uintptr_t velocity = 0x2B4;
        namespace bomb {
            inline uintptr_t name_offsets[3] = { 0x6A8, 0x10, 0x0 };
        }
        namespace rocket {
            inline uintptr_t name_offsets[3] = { 0x6B0, 0x758, 0x0 };
        }
    }

    inline std::string api_version;
    inline std::string api_channel;

    bool LoadFromApi(const char* url = "https://api.monkrel.cc/api/v1/offsets");
}