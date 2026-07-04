#include "structs.hpp"
#include "offsets.hpp"
#include "memory.hpp"
#include <thread>
#include <algorithm> 
#include <cmath>     
#include <mutex>
#include <atomic>
#include <chrono>

extern Memory mem;

namespace shared {
    extern std::vector<CachedEntity> Entities;
    extern std::vector<CachedRocket> Rockets;
    extern Matrix4x4 ViewMatrix;
    extern Matrix4x4 ViewMatrixAlt;
    extern Vector3 LocalPos;
    extern Vector3 LocalUnitPos;
    extern Vector3 NativePredictionPos;
    extern Vector3 CCIPPos;
    extern float LiveVelocity;
    extern int LocalTeam;
    extern std::mutex DataMutex;
    extern std::atomic<uintptr_t> TargetHijackPtr;
}

namespace settings {
    extern bool bEnableMemoryWrites;
    extern bool bEnableEntityHijack;
    extern bool bForceArcadeCrosshair;
    extern bool bForceAirLead;
    extern bool bForceTankESP;
    extern bool bForceThermals;
    extern bool bMidAirReload;
    extern bool bGhostCollision;
    extern bool bSpamScout;
    extern bool bThrustMult;
    extern bool bInternalsESP;
    extern int iInternalsMode;
    extern std::atomic<bool> bMindControlActive;
}

struct WTVertex {
    float x, y, z;
    uint32_t pad;
};

static bool IsSaneUnitPosition(const Vector3& pos) {
    return !std::isnan(pos.x) && !std::isnan(pos.y) && !std::isnan(pos.z)
        && !std::isinf(pos.x) && !std::isinf(pos.y) && !std::isinf(pos.z)
        && (std::fabs(pos.x) > 0.01f || std::fabs(pos.y) > 0.01f || std::fabs(pos.z) > 0.01f);
}

void FastViewThread() {
    float bulletVelocity = 800.0f;
    Vector3 lastLocalUnitPos = { 0, 0, 0 };

    while (true) {
        if (!mem.BaseAddress) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        const uintptr_t cGame = mem.ResolveCGamePtr();
        if (!mem.IsValidPtr(cGame)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        const Matrix4x4 viewAlt = mem.Read<Matrix4x4>(mem.BaseAddress + offsets::view_matrix_offset);
        uintptr_t camPtr = mem.Read<uintptr_t>(cGame + offsets::cgame::camera);
        Matrix4x4 viewM = mem.IsValidPtr(camPtr)
            ? mem.Read<Matrix4x4>(camPtr + offsets::camera::matrix)
            : viewAlt;

        Vector3 localUnitPos = lastLocalUnitPos;

        Vector3 ccip = { 0, 0, 0 };
        uintptr_t ballisticsPtr = mem.Read<uintptr_t>(cGame + offsets::cgame::ballistics);
        if (mem.IsValidPtr(ballisticsPtr)) {
            float velocity = mem.Read<float>(ballisticsPtr + offsets::ballistic::velocity);
            if (velocity > 10.0f && velocity < 5000.0f) bulletVelocity = velocity;
            ccip = mem.Read<Vector3>(ballisticsPtr + offsets::ballistic::bomb_impact_point);
        }

        uintptr_t localMgr = mem.Read<uintptr_t>(mem.BaseAddress + offsets::localplayer_offset);
        if (mem.IsValidPtr(localMgr)) {
            uintptr_t localUnit = mem.Read<uintptr_t>(localMgr + offsets::localplayer::localunit_offset);
            if (mem.IsValidPtr(localUnit)) {
                Vector3 unitPos = mem.Read<Vector3>(localUnit + offsets::unit::position_offset);
                if (IsSaneUnitPosition(unitPos)) {
                    localUnitPos = unitPos;
                    lastLocalUnitPos = unitPos;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(shared::DataMutex);
            shared::ViewMatrix = viewM;
            shared::ViewMatrixAlt = viewAlt;
            if (IsSaneUnitPosition(localUnitPos)) {
                shared::LocalPos = localUnitPos;
                shared::LocalUnitPos = localUnitPos;
            }
            shared::LiveVelocity = bulletVelocity;
            shared::CCIPPos = ccip;
        }
        shared::viewGeneration.fetch_add(1, std::memory_order_relaxed);

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
}

void CacheThread() {
    static uintptr_t cachedRealLocalUnit = 0;
    static bool restoreControlPending = false;

    while (true) {
        const auto iterationStart = std::chrono::steady_clock::now();

        if (!mem.BaseAddress) { std::this_thread::sleep_for(std::chrono::milliseconds(500)); continue; }

        const uintptr_t cGame = mem.ResolveCGamePtr();
        if (!mem.IsValidPtr(cGame)) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }

        if (settings::bEnableMemoryWrites) {
            uintptr_t hudPtr = mem.Read<uintptr_t>(mem.BaseAddress + offsets::hud_offset);
            if (mem.IsValidPtr(hudPtr)) {
                if (settings::bForceArcadeCrosshair) mem.Write<uint8_t>(hudPtr + offsets::hud::arcade_crosshair, 2);
                if (settings::bForceAirLead) {
                    mem.Write<uint8_t>(hudPtr + offsets::hud::can_select_unit, 1);
                    mem.Write<uint8_t>(hudPtr + offsets::hud::aircraft_distance, 2);
                    mem.Write<uint8_t>(hudPtr + offsets::hud::ground_to_air_prediction, 2);
                    mem.Write<uint8_t>(hudPtr + offsets::hud::air_to_air_prediction, 2);
                }
                if (settings::bForceTankESP) mem.Write<uint8_t>(hudPtr + offsets::hud::tank_esp, 2);
            }
        }

        uintptr_t localMgr = mem.Read<uintptr_t>(mem.BaseAddress + offsets::localplayer_offset);
        if (mem.IsValidPtr(localMgr)) {
            if (!settings::bMindControlActive || cachedRealLocalUnit == 0) {
                const uintptr_t tempUnit = mem.Read<uintptr_t>(localMgr + offsets::localplayer::localunit_offset);
                if (mem.IsValidPtr(tempUnit)) {
                    cachedRealLocalUnit = tempUnit;
                }
                else {
                    cachedRealLocalUnit = 0;
                }
            }
        }
        else {
            cachedRealLocalUnit = 0;
        }

        static Vector3 lastCachedLocalPos = { 0, 0, 0 };
        Vector3 trueLocalPos = lastCachedLocalPos;
        Vector3 trueLocalVel = { 0,0,0 };
        if (mem.IsValidPtr(cachedRealLocalUnit)) {
            Vector3 unitPos = mem.Read<Vector3>(cachedRealLocalUnit + offsets::unit::position_offset);
            if (IsSaneUnitPosition(unitPos)) {
                trueLocalPos = unitPos;
                lastCachedLocalPos = unitPos;

                uintptr_t lAirMov = mem.Read<uintptr_t>(cachedRealLocalUnit + offsets::unit::airmovement_offset);
                if (mem.IsValidPtr(lAirMov)) {
                    Vector3 aVel = mem.Read<Vector3>(lAirMov + offsets::unit::air_velocity_offset);
                    if (!std::isnan(aVel.x) && aVel.Length() < 2000.0f) trueLocalVel = aVel;
                }
            }
        }

        int count = mem.Read<int>(cGame + offsets::cgame::unitcount);
        if (count < 0) count = 0;
        if (count > 4096) count = 4096;
        shared::rawUnitCount.store(static_cast<uint32_t>(count), std::memory_order_relaxed);

        uintptr_t unitList = mem.Read<uintptr_t>(cGame + offsets::cgame::unitlist);
        std::vector<uintptr_t> ptrs(count);

        if (mem.IsValidPtr(unitList) && count > 0) {
            mem.ReadBuffer(unitList, ptrs.data(), count * sizeof(uintptr_t));
        }

        std::vector<CachedEntity> tempCache;
        int lTeam = -1;
        if (mem.IsValidPtr(cachedRealLocalUnit)) {
            const uint8_t localTeam = mem.Read<uint8_t>(cachedRealLocalUnit + offsets::unit::unitArmyNo_offset);
            if (localTeam != 0) lTeam = localTeam;
        }

        for (int i = 0; i < count; i++) {
            uintptr_t ptr = ptrs[i];
            if (!mem.IsValidPtr(ptr)) continue;

            short state = mem.Read<short>(ptr + offsets::unit::unitState_offset);
            if (state < 0 || state > 3) continue;

            Vector3 pos = mem.Read<Vector3>(ptr + offsets::unit::position_offset);

            if (std::isnan(pos.x) || std::isnan(pos.y) || std::isnan(pos.z)) continue;
            if (std::isinf(pos.x) || std::isinf(pos.y) || std::isinf(pos.z)) continue;

            uint8_t team = mem.Read<uint8_t>(ptr + offsets::unit::unitArmyNo_offset);
            if (team == 0) continue;

            if (cachedRealLocalUnit != 0 && ptr == cachedRealLocalUnit) {
                lTeam = team;
                if (IsSaneUnitPosition(pos)) {
                    trueLocalPos = pos;
                    lastCachedLocalPos = pos;
                }
                continue;
            }

            CachedEntity ent;
            ent.pointer = ptr;
            ent.position = pos;
            ent.team = team;

            uintptr_t infoPtr = mem.Read<uintptr_t>(ptr + offsets::unit::info_offset);
            if (mem.IsValidPtr(infoPtr)) {
                uintptr_t namePtr = mem.Read<uintptr_t>(infoPtr + offsets::wtinfo::FullName);
                if (mem.IsValidPtr(namePtr)) {
                    std::string raw = mem.ReadString(namePtr, 64);
                    size_t slash = raw.find_last_of("/\\");
                    if (slash != std::string::npos) raw = raw.substr(slash + 1);
                    size_t ext = raw.find('.');
                    if (ext != std::string::npos) raw = raw.substr(0, ext);
                    std::replace(raw.begin(), raw.end(), '_', ' ');
                    ent.name = raw;
                }
            }

            uintptr_t groundMov = mem.Read<uintptr_t>(ptr + offsets::unit::groundmovement_offset);
            if (mem.IsValidPtr(groundMov)) {
                ent.velocity = mem.Read<Vector3>(groundMov + offsets::unit::ground_velocity_offset);
                ent.isAir = false;
            }
            else {
                uintptr_t airMov = mem.Read<uintptr_t>(ptr + offsets::unit::airmovement_offset);
                if (mem.IsValidPtr(airMov)) {
                    ent.velocity = mem.Read<Vector3>(airMov + offsets::unit::air_velocity_offset);
                    ent.isAir = true;
                }
            }

            ent.rotation = mem.Read<Matrix3x3>(ptr + offsets::unit::rotation_matrix);
            ent.bbMin = mem.Read<Vector3>(ptr + offsets::unit::bbmin_offset);
            ent.bbMax = mem.Read<Vector3>(ptr + offsets::unit::bbmax_offset);
            ent.reloadProgress = mem.Read<float>(ptr + offsets::unit::visualReloadProgress_offset);
            ent.isValid = true;

            if (settings::bInternalsESP && !ent.isAir) {
                uintptr_t dm_base = mem.Read<uintptr_t>(ptr + offsets::damage_model::ptrs[0]);
                dm_base = mem.Read<uintptr_t>(dm_base + offsets::damage_model::ptrs[1]);

                uintptr_t trans_base = mem.Read<uintptr_t>(ptr + offsets::damage_model::transform_info[0]);
                if (mem.IsValidPtr(trans_base)) {
                    trans_base = mem.Read<uintptr_t>(trans_base + offsets::damage_model::transform_info[1]);
                }

                if (mem.IsValidPtr(dm_base) && mem.IsValidPtr(trans_base)) {
                    uintptr_t dm_array_ptr = mem.Read<uintptr_t>(dm_base + offsets::damage_model::ptrs[2]);
                    uint32_t dm_count = mem.Read<uint32_t>(dm_base + offsets::damage_model::count[2]);

                    if (mem.IsValidPtr(dm_array_ptr) && dm_count > 0 && dm_count < 1000) {
                        for (uint32_t j = 0; j < dm_count; j++) {
                            uintptr_t current_dm = dm_array_ptr + (j * offsets::damage_model::struct_size);

                            uintptr_t name_ptr = mem.Read<uintptr_t>(current_dm + offsets::damage_model::dm_name);
                            if (mem.IsValidPtr(name_ptr)) {
                                std::string pName = mem.ReadString(name_ptr, 24);
                                if (pName.length() < 3) continue;

                                std::transform(pName.begin(), pName.end(), pName.begin(), ::tolower);

                                if (pName.find("crew") != std::string::npos ||
                                    pName.find("ammo") != std::string::npos ||
                                    pName.find("powder") != std::string::npos ||
                                    pName.find("engine") != std::string::npos ||
                                    pName.find("transmission") != std::string::npos ||
                                    pName.find("drive") != std::string::npos ||
                                    pName.find("loader") != std::string::npos ||
                                    pName.find("gunner") != std::string::npos ||
                                    pName.find("driver") != std::string::npos ||
                                    pName.find("commander") != std::string::npos ||
                                    pName.find("breech") != std::string::npos)
                                {
                                    uint16_t t_index = mem.Read<uint16_t>(current_dm + offsets::damage_model::transform_index);

                                    InternalPart p;
                                    p.name = pName;
                                    p.bbMin = mem.Read<Vector3>(current_dm + offsets::damage_model::bbmin);
                                    p.bbMax = mem.Read<Vector3>(current_dm + offsets::damage_model::bbmax);
                                    p.position = mem.Read<Vector3>(current_dm + offsets::damage_model::dm_position);
                                    p.transform = mem.Read<Matrix4x4>(trans_base + offsets::damage_model::transform_matrix_offset + (t_index * 64));

                                    if (settings::iInternalsMode == 1) {
                                        uint32_t vCount = mem.Read<uint32_t>(current_dm + offsets::damage_model::vertices_count);
                                        uint32_t iCount = mem.Read<uint32_t>(current_dm + offsets::damage_model::indices_count);

                                        if (vCount > 0 && vCount < 3000 && iCount > 0 && iCount < 10000) {
                                            uintptr_t vPtr = mem.Read<uintptr_t>(current_dm + offsets::damage_model::vertices_ptr);
                                            uintptr_t iPtr = mem.Read<uintptr_t>(current_dm + offsets::damage_model::indices_ptr);

                                            if (mem.IsValidPtr(vPtr) && mem.IsValidPtr(iPtr)) {
                                                std::vector<WTVertex> rawVerts(vCount);
                                                mem.ReadBuffer(vPtr, rawVerts.data(), vCount * sizeof(WTVertex));

                                                p.vertices.resize(vCount);
                                                for (uint32_t v = 0; v < vCount; v++) {
                                                    p.vertices[v] = { rawVerts[v].x, rawVerts[v].y, rawVerts[v].z };
                                                }

                                                p.indices.resize(iCount);
                                                mem.ReadBuffer(iPtr, p.indices.data(), iCount * sizeof(uint16_t));
                                            }
                                        }
                                    }

                                    ent.internals.push_back(p);
                                }
                            }
                        }
                    }
                }
            }

            tempCache.push_back(ent);
        }

        // broken missle esp rmbr 2 fix
        std::vector<CachedRocket> tempRockets;
        uint32_t rCount = mem.Read<uint32_t>(mem.BaseAddress + offsets::g_rockets_index);

        if (rCount > 0 && rCount < 500) {
            // globals count is at base, list ptr is exactly 8 bytes after count
            uintptr_t rListPtr = mem.Read<uintptr_t>(mem.BaseAddress + offsets::g_rockets_index - 0x8);
            if (!mem.IsValidPtr(rListPtr)) {
                rListPtr = mem.Read<uintptr_t>(mem.BaseAddress + offsets::g_rockets_index + 0x8); // Fallback
            }

            if (mem.IsValidPtr(rListPtr)) {
                std::vector<uintptr_t> rptrs(rCount);
                mem.ReadBuffer(rListPtr, rptrs.data(), rCount * sizeof(uintptr_t));

                for (uint32_t i = 0; i < rCount; i++) {
                    uintptr_t ptr = rptrs[i];
                    if (!mem.IsValidPtr(ptr)) continue;

                    CachedRocket r;
                    r.pointer = ptr;
                    r.position = mem.Read<Vector3>(ptr + offsets::shell::position);
                    r.velocity = mem.Read<Vector3>(ptr + offsets::shell::velocity);

                    if (std::isnan(r.position.x) || r.position.x == 0.0f) continue; // Filter dead/inactive missiles

                    uintptr_t nPtr1 = mem.Read<uintptr_t>(ptr + offsets::shell::rocket::name_offsets[0]);
                    if (mem.IsValidPtr(nPtr1)) {
                        uintptr_t nPtr2 = mem.Read<uintptr_t>(nPtr1 + offsets::shell::rocket::name_offsets[1]);
                        if (mem.IsValidPtr(nPtr2)) {
                            std::string rawName = mem.ReadString(nPtr2 + offsets::shell::rocket::name_offsets[2], 32);

                            size_t slash = rawName.find_last_of("/\\");
                            if (slash != std::string::npos) rawName = rawName.substr(slash + 1);

                            r.name = rawName;
                        }
                    }

                    r.distanceToLocal = r.position.Distance(trueLocalPos);
                    r.isThreat = false;
                    r.timeToImpact = 999.0f;

                    // MAWS ALGORITHM: broken need fix
                    if (r.distanceToLocal < 15000.0f && trueLocalVel.Length() > 10.0f) { // Within 15km and you are actually moving
                        Vector3 relPos = { r.position.x - trueLocalPos.x, r.position.y - trueLocalPos.y, r.position.z - trueLocalPos.z };
                        Vector3 relVel = { r.velocity.x - trueLocalVel.x, r.velocity.y - trueLocalVel.y, r.velocity.z - trueLocalVel.z };

                        float dotPosVel = (relPos.x * relVel.x) + (relPos.y * relVel.y) + (relPos.z * relVel.z);
                        if (dotPosVel < 0) { // Dot product is negative ONLY if object is moving towards you
                            float velMagSq = (relVel.x * relVel.x) + (relVel.y * relVel.y) + (relVel.z * relVel.z);
                            if (velMagSq > 0) {
                                float t_cpa = -dotPosVel / velMagSq; // Time to Closest Point of Approach
                                if (t_cpa > 0.0f && t_cpa < 12.0f) { // If it hits us in less than 12 seconds
                                    Vector3 cpaPos = { relPos.x + (relVel.x * t_cpa), relPos.y + (relVel.y * t_cpa), relPos.z + (relVel.z * t_cpa) };
                                    float cpaDist = cpaPos.Length();

                                    if (cpaDist < 100.0f) { // If it passes within 100 meters (Danger Zone)
                                        r.isThreat = true;
                                        r.timeToImpact = t_cpa;
                                    }
                                }
                            }
                        }
                    }
                    tempRockets.push_back(r);
                }
            }
        }

        if (!settings::bEnableEntityHijack) {
            settings::bMindControlActive = false;
        }

        if (settings::bEnableMemoryWrites && mem.IsValidPtr(localMgr) && cachedRealLocalUnit != 0) {
            if (settings::bForceThermals) mem.Write<int>(cachedRealLocalUnit + 0x768, 1);
            if (settings::bMidAirReload) mem.Write<bool>(cachedRealLocalUnit + 0x35F0, true);
            if (settings::bGhostCollision) mem.Write<bool>(cachedRealLocalUnit + 0x3E30, true);
            if (settings::bSpamScout) mem.Write<float>(cachedRealLocalUnit + 0x1030, 0.0f);
            if (settings::bThrustMult) mem.Write<float>(cachedRealLocalUnit + 0x3D68, 1.15f);

            if (settings::bEnableEntityHijack && settings::bMindControlActive && shared::TargetHijackPtr.load() != 0) {
                mem.Write<uintptr_t>(localMgr + offsets::localplayer::localunit_offset, shared::TargetHijackPtr.load());
                restoreControlPending = true;
            }
            else if (restoreControlPending) {
                mem.Write<uintptr_t>(localMgr + offsets::localplayer::localunit_offset, cachedRealLocalUnit);
                restoreControlPending = false;
            }
        }
        else if (!settings::bEnableMemoryWrites) {
            settings::bMindControlActive = false;
            if (restoreControlPending && mem.IsValidPtr(localMgr) && cachedRealLocalUnit != 0) {
                mem.Write<uintptr_t>(localMgr + offsets::localplayer::localunit_offset, cachedRealLocalUnit);
                restoreControlPending = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(shared::DataMutex);
            shared::LocalTeam = lTeam;
            if (IsSaneUnitPosition(trueLocalPos)) {
                shared::LocalUnitPos = trueLocalPos;
            }
            shared::Entities = std::move(tempCache);
            shared::Rockets = std::move(tempRockets);
            shared::entityCacheTick.store(GetTickCount64(), std::memory_order_relaxed);
        }
        shared::cachedEntityCount.store(static_cast<uint32_t>(tempCache.size()), std::memory_order_relaxed);
        shared::entityGeneration.fetch_add(1, std::memory_order_relaxed);

        const auto iterationEnd = std::chrono::steady_clock::now();
        perf::cacheMs.store(
            static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(iterationEnd - iterationStart).count()),
            std::memory_order_relaxed
        );

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}