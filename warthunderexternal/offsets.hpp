#pragma once
#include <cstdint>

namespace offsets {
    constexpr uintptr_t cgame_offset = 0x6c108b8;
    constexpr uintptr_t view_matrix_offset = 0x6c73df0;
    constexpr uintptr_t hud_offset = 0x6c0fea8;
    constexpr uintptr_t localplayer_offset = 0x6beda30;
    constexpr uintptr_t alllistdata_offset = 0x6e32478;
    constexpr uintptr_t c_optics_offset = 0x6c12320;
    constexpr uintptr_t entity_manager_offset = 0x6e1cb60;
    constexpr uintptr_t game_time_offset = 0xffff80091c910000; // Unused

    constexpr uintptr_t g_bombs_index = 0x6bf04d8;
    constexpr uintptr_t g_bullets_index = 0x6be9c80;
    constexpr uintptr_t g_rockets_index = 0x6bf1920;
    constexpr uintptr_t yaw_offset = 0x6c13c78;
    constexpr uintptr_t yaw_offset_y = 0x6c13c7c;

    namespace hud {
        constexpr uint32_t can_select_unit = 0x20;
        constexpr uint32_t arcade_crosshair = 0x2B0;
        constexpr uint32_t aircraft_distance = 0x2AA + 0x8;
        constexpr uint32_t ground_to_air_prediction = 0x2B0 + 0x8;
        constexpr uint32_t air_to_air_prediction = 0x29D + 0x8;
        constexpr uint32_t tank_esp = 0x1E4;
    }
    namespace cgame {
        constexpr uint32_t ballistics = 0x3f0;
        constexpr uint32_t camera = 0x670;
        constexpr uint32_t unitlist = 0x340;
        constexpr uint32_t unitcount = 0x350;
    }
    namespace camera {
        constexpr uint32_t matrix = 0x1C0;
    }
    namespace ballistic {
        constexpr uint32_t velocity = 0x2048;
        constexpr uint32_t bomb_impact_point = 0x1C14;
        constexpr uint32_t selected_unit_prediction = 0x6B0;
    }
    namespace localplayer {
        constexpr uint32_t localunit_offset = 0x8E8;
    }
    namespace unit {
        constexpr uint32_t groundmovement_offset = 0x1E00;
        constexpr uint32_t ground_velocity_offset = 0x54;
        constexpr uint32_t airmovement_offset = 0xD18;
        constexpr uint32_t air_velocity_offset = 0x15A8;
        constexpr uint32_t bbmin_offset = 0x240;
        constexpr uint32_t bbmax_offset = 0x24C;
        constexpr uint32_t rotation_matrix = 0xCE4;
        constexpr uint32_t position_offset = 0xD08;
        constexpr uint32_t info_offset = 0xFC8;
        constexpr uint32_t unitArmyNo_offset = 0xFB8;
        constexpr uint32_t unitState_offset = 0xF38;
        constexpr uint32_t visualReloadProgress_offset = 0xAB8;
    }
    namespace wtinfo {
        constexpr uint32_t FullName = 0x0018;
    }
    namespace damage_model {
        inline constexpr uintptr_t ptrs[] = { 0x1048, 0x58, 0xA0 };
        inline constexpr uintptr_t count[] = { 0x1048, 0x58, 0xB0 };
        inline constexpr uintptr_t transform_info[] = { 0x238, 0x0 };
        inline constexpr uintptr_t transform_matrix_offset = 0x40;
        inline constexpr uintptr_t next_damage_model = 0x8;
        inline constexpr uintptr_t transform_index = 0x4;
        inline constexpr uintptr_t forward = 0x10;
        inline constexpr uintptr_t up = 0x1C;
        inline constexpr uintptr_t left = 0x28;
        inline constexpr uintptr_t dm_position = 0x34;
        inline constexpr uintptr_t bbmin = 0x40;
        inline constexpr uintptr_t bbmax = 0x4C;
        inline constexpr uintptr_t vertices_ptr = 0x80;
        inline constexpr uintptr_t vertices_count = 0x88;
        inline constexpr uintptr_t indices_ptr = 0x90;
        inline constexpr uintptr_t indices_count = 0x98;
        inline constexpr uintptr_t dm_index = 0xBC;
        inline constexpr uintptr_t dm_name = 0xC0;
        inline constexpr uintptr_t struct_size = 208;
    }
    // broken missile esp
    namespace shell {
        inline constexpr uintptr_t position = 0x190;
        inline constexpr uintptr_t velocity = 0x1AC;
        namespace bomb {
            inline constexpr uintptr_t name_offsets[] = { 0x670, 0x10, 0x0 };
        }
        namespace rocket {
            inline constexpr uintptr_t name_offsets[] = { 0x678, 0x50, 0x0 };
        }
    }
}