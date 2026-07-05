#include "structs.hpp"
#include "memory.hpp"
#include "input.hpp"
#include "imgui/imgui.h"
#include <cmath>
#include <string>

extern int ScreenWidth;
extern int ScreenHeight;

static bool WorldToScreenMatrix(const Vector3& pos, ImVec2& out, const Matrix4x4& vm) {
    const float cw = pos.x * vm.m[3] + pos.y * vm.m[7] + pos.z * vm.m[11] + vm.m[15];
    if (cw < 0.01f) return false;
    const float invW = 1.0f / cw;
    out.x = ((pos.x * vm.m[0] + pos.y * vm.m[4] + pos.z * vm.m[8] + vm.m[12]) * invW * 0.5f + 0.5f) * (float)ScreenWidth;
    out.y = (0.5f - (pos.x * vm.m[1] + pos.y * vm.m[5] + pos.z * vm.m[9] + vm.m[13]) * invW * 0.5f) * (float)ScreenHeight;
    return true;
}

bool WorldToScreen(const Vector3& pos, ImVec2& out, const Matrix4x4& vm) {
    return WorldToScreenMatrix(pos, out, vm);
}

static bool ProjectToScreen(const Vector3& pos, ImVec2& out, const Matrix4x4& primary, const Matrix4x4& fallback) {
    if (WorldToScreenMatrix(pos, out, primary)) return true;
    return WorldToScreenMatrix(pos, out, fallback);
}

Vector3 TransformVec(const Vector3& v, const Matrix3x3& mat, const Vector3& pos) {
    return { v.x * mat.m[0] + v.y * mat.m[3] + v.z * mat.m[6] + pos.x,
             v.x * mat.m[1] + v.y * mat.m[4] + v.z * mat.m[7] + pos.y,
             v.x * mat.m[2] + v.y * mat.m[5] + v.z * mat.m[8] + pos.z };
}

Vector3 TransformVec4x4(const Vector3& v, const Matrix4x4& mat) {
    return {
        v.x * mat.m[0] + v.y * mat.m[4] + v.z * mat.m[8] + mat.m[12],
        v.x * mat.m[1] + v.y * mat.m[5] + v.z * mat.m[9] + mat.m[13],
        v.x * mat.m[2] + v.y * mat.m[6] + v.z * mat.m[10] + mat.m[14]
    };
}

static bool IsFiniteVec3(const Vector3& v) {
    return !std::isnan(v.x) && !std::isnan(v.y) && !std::isnan(v.z)
        && !std::isinf(v.x) && !std::isinf(v.y) && !std::isinf(v.z);
}

static bool IsSaneUnitPosition(const Vector3& pos) {
    return !std::isnan(pos.x) && !std::isnan(pos.y) && !std::isnan(pos.z)
        && !std::isinf(pos.x) && !std::isinf(pos.y) && !std::isinf(pos.z)
        && (std::fabs(pos.x) > 0.01f || std::fabs(pos.y) > 0.01f || std::fabs(pos.z) > 0.01f);
}

static Vector3 SanitizeVelocity(const Vector3& vel, bool isAir) {
    if (!IsFiniteVec3(vel)) return { 0, 0, 0 };

    const float vLen = vel.Length();
    const float maxVel = isAir ? 900.0f : 120.0f;
    if (vLen > maxVel && vLen > 0.001f) {
        return vel * (maxVel / vLen);
    }
    return vel;
}

static Vector3 PredictEntityPos(const CachedEntity& ent) {
    const uint64_t cacheTick = shared::entityCacheTick.load(std::memory_order_relaxed);
    if (cacheTick == 0) return ent.position;

    float ageSec = (GetTickCount64() - cacheTick) / 1000.0f;
    if (ageSec < 0.0f) ageSec = 0.0f;
    if (ageSec > 0.12f) ageSec = 0.12f;

    const Vector3 vel = SanitizeVelocity(ent.velocity, ent.isAir);
    const Vector3 predicted = ent.position + vel * ageSec;
    if (!IsFiniteVec3(predicted)) return ent.position;
    return predicted;
}

struct BallisticLead {
    Vector3 aimPoint{};
    float flightTime = 0.0f;
    float range3d = 0.0f;
};

static BallisticLead SolveBallisticLead(
    const Vector3& shooterPos,
    const Vector3& targetPos,
    const Vector3& targetVel,
    float bulletSpeed,
    bool isAir,
    bool enableLead,
    bool enableDrop,
    float gravityScale)
{
    BallisticLead result{};
    result.aimPoint = targetPos;
    if (!IsSaneUnitPosition(shooterPos) || !IsFiniteVec3(targetPos) || bulletSpeed < 10.0f) {
        return result;
    }

    const Vector3 vel = SanitizeVelocity(targetVel, isAir);
    const float g = 9.81f * gravityScale;
    const bool leadOn = enableLead && vel.Length() > 0.05f;
    const bool dropOn = enableDrop;

    Vector3 aim = targetPos;
    float t = 0.0f;

    // Improved iterative solver for lead + drop
    for (int pass = 0; pass < 3; ++pass) {
        const float dist = shooterPos.Distance(aim);
        if (dist < 0.5f) break;

        t = dist / bulletSpeed;
        if (t <= 0.0f || !std::isfinite(t)) break;

        aim = targetPos;
        if (leadOn) {
            aim = aim + vel * t;
        }
        if (dropOn) {
            aim.y += 0.5f * g * t * t;
        }
    }

    result.aimPoint = aim;
    result.flightTime = t;
    result.range3d = shooterPos.Distance(targetPos);
    return result;
}

bool IsAimingAtMe(const Vector3& enemyPos, const Vector3& localPos, const Matrix3x3& enemyRot) {
    Vector3 fwd = { enemyRot.m[0], enemyRot.m[1], enemyRot.m[2] };
    float fLen = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
    if (fLen > 0.001f) { fwd.x /= fLen; fwd.y /= fLen; fwd.z /= fLen; }

    Vector3 dir = { localPos.x - enemyPos.x, localPos.y - enemyPos.y, localPos.z - enemyPos.z };
    float dLen = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (dLen > 0.001f) { dir.x /= dLen; dir.y /= dLen; dir.z /= dLen; }

    return ((fwd.x * dir.x) + (fwd.y * dir.y) + (fwd.z * dir.z) > 0.96f);
}

void MoveMouse(float x, float y) {
    float dx = x - (float)ScreenWidth / 2.0f;
    float dy = y - (float)ScreenHeight / 2.0f;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 2.0f) return;

    float appliedSmooth = settings::aimSmooth;
    Input::MoveMouseRelative(
        static_cast<int>(dx / appliedSmooth),
        static_cast<int>(dy / appliedSmooth)
    );
}

static float CalcHorizontalRange(const Vector3& target, const Vector3& localUnit) {
    const float dx = target.x - localUnit.x;
    const float dz = target.z - localUnit.z;
    return std::sqrt(dx * dx + dz * dz);
}

void RenderESP(ImDrawList* draw) {
    std::vector<CachedEntity> localEntities;
    std::vector<CachedRocket> localRockets;
    Matrix4x4 vm, vmAlt; Vector3 lPos, localUnitPos, ccip; float bVel; int lTeam;

    {
        std::lock_guard<std::mutex> lock(shared::DataMutex);
        localEntities = shared::Entities;
        localRockets = shared::Rockets; // Get synced missiles
        vm = shared::ViewMatrix;
        vmAlt = shared::ViewMatrixAlt;
        lPos = shared::LocalPos;
        localUnitPos = shared::LocalUnitPos;
        ccip = shared::CCIPPos; bVel = shared::LiveVelocity; lTeam = shared::LocalTeam;
    }

    const bool hasLocalUnit = IsSaneUnitPosition(localUnitPos);

    // maws system
    if (settings::bMissileESP) {
        for (const auto& r : localRockets) {
            ImVec2 sPos;
            if (WorldToScreen(r.position, sPos, vm)) {
                // Draw Diamond ESP
                draw->AddTriangleFilled(
                    ImVec2(sPos.x, sPos.y - 8), ImVec2(sPos.x - 6, sPos.y + 6), ImVec2(sPos.x + 6, sPos.y + 6),
                    ImColor(255, 50, 50, 255)
                );

                const std::string rText = r.name + "[" + std::to_string(static_cast<int>(r.distanceToLocal / 1000.0f)) + "km]";
                ImU32 missileCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings::col_ESPText[0], settings::col_ESPText[1], settings::col_ESPText[2], settings::col_ESPText[3]));
                draw->AddText(ImVec2(sPos.x + 10, sPos.y - 8), missileCol, rText.c_str());

                if (r.isThreat) {
                    char warn[96];
                    _snprintf_s(warn, _TRUNCATE, "MISSILE INCOMING: %.1fs", r.timeToImpact);
                    ImVec2 tSize = ImGui::CalcTextSize(warn);

                    // red flash
                    draw->AddText(ImGui::GetFont(), 32.0f, ImVec2((ScreenWidth / 2.0f) - (tSize.x / 2.0f), (ScreenHeight / 2.0f) - 150.0f), ImColor(255, 0, 0, 255), warn);

                    // Threat Line from Screen Center directly to the incoming missile
                    draw->AddLine(ImVec2(ScreenWidth / 2.0f, ScreenHeight / 2.0f), sPos, ImColor(255, 0, 0, 200), 3.0f);
                }
            }
        }
    }

    if (settings::bCCIP && (ccip.x != 0 || ccip.y != 0 || ccip.z != 0)) {
        ImVec2 screenCCIP;
        if (WorldToScreen(ccip, screenCCIP, vm)) {
            draw->AddCircle(screenCCIP, 6.0f, ImColor(255, 50, 50, 255), 12, 2.0f);
            ImU32 ccipCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings::col_ESPText[0], settings::col_ESPText[1], settings::col_ESPText[2], settings::col_ESPText[3]));
            draw->AddText(ImVec2(screenCCIP.x + 8, screenCCIP.y - 8), ccipCol, "CCIP");
        }
    }

    ImVec2 bestTarget = { 0,0 }; float minDist = settings::aimFov;

    for (const auto& ent : localEntities) {
        const bool isTeammate = settings::bAutoTeam
            ? (lTeam > 0 && ent.team == lTeam)
            : (ent.team == settings::ManualTeam);

        if (isTeammate && !settings::bEspTeammates) continue;

        if (!settings::bEspBots && ent.isBot) continue;

        if (!ent.isAir && !settings::bEspGround) continue;

        ImVec2 screenPos;
        if (!ProjectToScreen(ent.position, screenPos, vm, vmAlt)) continue;

        CachedEntity drawEnt = ent;
        drawEnt.position = PredictEntityPos(ent);

        float distMeters = 0.0f;
        if (hasLocalUnit) {
            distMeters = CalcHorizontalRange(ent.position, localUnitPos);
        }
        const float clipW = ent.position.x * vm.m[3] + ent.position.y * vm.m[7]
            + ent.position.z * vm.m[11] + vm.m[15];
        if (distMeters <= 0.5f && clipW > 0.5f && std::isfinite(clipW)) {
            distMeters = clipW;
        }

        ImU32 boxColor = ImGui::ColorConvertFloat4ToU32(
            isTeammate ? *(ImVec4*)settings::col_BoxTeam : *(ImVec4*)settings::col_BoxVis);

        Vector3 c[8] = { {ent.bbMin.x, ent.bbMin.y, ent.bbMin.z}, {ent.bbMax.x, ent.bbMin.y, ent.bbMin.z}, {ent.bbMax.x, ent.bbMax.y, ent.bbMin.z}, {ent.bbMin.x, ent.bbMax.y, ent.bbMin.z}, {ent.bbMin.x, ent.bbMin.y, ent.bbMax.z}, {ent.bbMax.x, ent.bbMin.y, ent.bbMax.z}, {ent.bbMax.x, ent.bbMax.y, ent.bbMax.z}, {ent.bbMin.x, ent.bbMax.y, ent.bbMax.z} };
        ImVec2 s[8] = {};
        int visibleCorners = 0;
        float mix = 10000.0f, max = -10000.0f, miy = 10000.0f, may = -10000.0f;
        for (int k = 0; k < 8; k++) {
            if (!ProjectToScreen(TransformVec(c[k], drawEnt.rotation, drawEnt.position), s[k], vm, vmAlt)) continue;
            visibleCorners++;
            if (s[k].x < mix) mix = s[k].x; if (s[k].x > max) max = s[k].x;
            if (s[k].y < miy) miy = s[k].y; if (s[k].y > may) may = s[k].y;
        }

        const bool validBox = visibleCorners > 0 && max > mix && may > miy;
        if (!validBox) {
            const float fallback = 18.0f;
            mix = screenPos.x - fallback;
            max = screenPos.x + fallback;
            miy = screenPos.y - fallback;
            may = screenPos.y + fallback;
        }

        if (settings::bDangerWarning && IsAimingAtMe(drawEnt.position, lPos, drawEnt.rotation)) {
            boxColor = ImColor(255, 0, 0);
            ImVec2 cent((float)ScreenWidth / 2.0f, (float)ScreenHeight); ImVec2 es;
            if (WorldToScreen(drawEnt.position, es, vm)) draw->AddLine(cent, es, ImColor(255, 0, 0, 150), 2.0f);
        }

        if (settings::bEsp) {
            if (settings::bInternalsESP && !ent.internals.empty()) {
                for (const auto& part : ent.internals) {
                    ImU32 partCol = ImColor(255, 255, 255, 240);
                    std::string n = part.name;

                    if (n.find("ammo") != std::string::npos || n.find("powder") != std::string::npos)
                        partCol = ImColor(255, 50, 50, 240);
                    else if (n.find("engine") != std::string::npos || n.find("transmission") != std::string::npos)
                        partCol = ImColor(255, 150, 50, 240);
                    else if (n.find("crew") != std::string::npos || n.find("gunner") != std::string::npos || n.find("driver") != std::string::npos || n.find("loader") != std::string::npos || n.find("commander") != std::string::npos)
                        partCol = ImColor(50, 200, 255, 240);
                    else if (n.find("breech") != std::string::npos || n.find("drive") != std::string::npos)
                        partCol = ImColor(200, 50, 255, 240);

                    auto TransformPart = [](const Vector3& v, const Matrix4x4& mat, const Vector3& offset) -> Vector3 {
                        return {
                            v.x * mat.m[0] + v.y * mat.m[4] + v.z * mat.m[8] + offset.x,
                            v.x * mat.m[1] + v.y * mat.m[5] + v.z * mat.m[9] + offset.y,
                            v.x * mat.m[2] + v.y * mat.m[6] + v.z * mat.m[10] + offset.z
                        };
                        };

                    if (settings::iInternalsMode == 0) {
                        Vector3 pC[8] = {
                            {part.bbMin.x, part.bbMin.y, part.bbMin.z}, {part.bbMax.x, part.bbMin.y, part.bbMin.z},
                            {part.bbMax.x, part.bbMax.y, part.bbMin.z}, {part.bbMin.x, part.bbMax.y, part.bbMin.z},
                            {part.bbMin.x, part.bbMin.y, part.bbMax.z}, {part.bbMax.x, part.bbMin.y, part.bbMax.z},
                            {part.bbMax.x, part.bbMax.y, part.bbMax.z}, {part.bbMin.x, part.bbMax.y, part.bbMax.z}
                        };

                        ImVec2 pS[8]; bool validPartBox = true;
                        for (int k = 0; k < 8; k++) {
                            Vector3 tankLocal = TransformPart(pC[k], part.transform, part.position);
                            Vector3 worldPos = TransformVec(tankLocal, drawEnt.rotation, drawEnt.position);
                            if (!WorldToScreen(worldPos, pS[k], vm)) { validPartBox = false; break; }
                        }

                        if (validPartBox) {
                            ImColor fadeCol = ImColor(partCol); fadeCol.Value.w = 0.35f; ImU32 boxAlpha = fadeCol;
                            draw->AddLine(pS[0], pS[1], boxAlpha); draw->AddLine(pS[1], pS[2], boxAlpha);
                            draw->AddLine(pS[2], pS[3], boxAlpha); draw->AddLine(pS[3], pS[0], boxAlpha);
                            draw->AddLine(pS[4], pS[5], boxAlpha); draw->AddLine(pS[5], pS[6], boxAlpha);
                            draw->AddLine(pS[6], pS[7], boxAlpha); draw->AddLine(pS[7], pS[4], boxAlpha);
                            draw->AddLine(pS[0], pS[4], boxAlpha); draw->AddLine(pS[1], pS[5], boxAlpha);
                            draw->AddLine(pS[2], pS[6], boxAlpha); draw->AddLine(pS[3], pS[7], boxAlpha);
                        }
                    }
                    else if (settings::iInternalsMode == 1 && !part.vertices.empty() && !part.indices.empty()) {
                        ImColor fadeCol = ImColor(partCol); fadeCol.Value.w = 0.40f; ImU32 meshAlpha = fadeCol;

                        auto isGarbage = [](const Vector3& v, const Vector3& bmin, const Vector3& bmax) {
                            float margin = 0.5f;
                            return v.x < bmin.x - margin || v.x > bmax.x + margin ||
                                v.y < bmin.y - margin || v.y > bmax.y + margin ||
                                v.z < bmin.z - margin || v.z > bmax.z + margin;
                            };

                        for (size_t m = 0; m + 2 < part.indices.size(); m += 3) {
                            uint16_t i1 = part.indices[m];
                            uint16_t i2 = part.indices[m + 1];
                            uint16_t i3 = part.indices[m + 2];

                            if (i1 < part.vertices.size() && i2 < part.vertices.size() && i3 < part.vertices.size()) {
                                Vector3 v1 = part.vertices[i1];
                                Vector3 v2 = part.vertices[i2];
                                Vector3 v3 = part.vertices[i3];

                                if (isGarbage(v1, part.bbMin, part.bbMax) ||
                                    isGarbage(v2, part.bbMin, part.bbMax) ||
                                    isGarbage(v3, part.bbMin, part.bbMax)) {
                                    continue;
                                }

                                Vector3 l1 = TransformPart(v1, part.transform, part.position);
                                Vector3 l2 = TransformPart(v2, part.transform, part.position);
                                Vector3 l3 = TransformPart(v3, part.transform, part.position);

                                Vector3 w1 = TransformVec(l1, drawEnt.rotation, drawEnt.position);
                                Vector3 w2 = TransformVec(l2, drawEnt.rotation, drawEnt.position);
                                Vector3 w3 = TransformVec(l3, drawEnt.rotation, drawEnt.position);

                                ImVec2 s1, s2, s3;
                                if (WorldToScreen(w1, s1, vm) && WorldToScreen(w2, s2, vm) && WorldToScreen(w3, s3, vm)) {
                                    draw->AddTriangleFilled(s1, s2, s3, meshAlpha);
                                }
                            }
                        }
                    }

                    Vector3 centerPart = { (part.bbMin.x + part.bbMax.x) / 2.0f, (part.bbMin.y + part.bbMax.y) / 2.0f, (part.bbMin.z + part.bbMax.z) / 2.0f };
                    Vector3 centerTank = TransformPart(centerPart, part.transform, part.position);
                    Vector3 centerWorld = TransformVec(centerTank, drawEnt.rotation, drawEnt.position);

                    ImVec2 partScreen;
                    if (WorldToScreen(centerWorld, partScreen, vm)) {
                        draw->AddCircleFilled(partScreen, 2.5f, partCol);
                        if (settings::bInternalsName) {
                            draw->AddText(ImGui::GetFont(), 12.0f, ImVec2(partScreen.x + 5, partScreen.y - 6), partCol, n.c_str(), NULL, 0.0f, nullptr);
                        }
                    }
                }
            }

            if (settings::bFacing) {
                Vector3 forwardVec = { ent.rotation.m[0], ent.rotation.m[1], ent.rotation.m[2] };
                float len = std::sqrt(forwardVec.x * forwardVec.x + forwardVec.y * forwardVec.y + forwardVec.z * forwardVec.z);
                if (len > 0.01f) {
                    forwardVec.x /= len; forwardVec.y /= len; forwardVec.z /= len;
                    Vector3 endPos = { drawEnt.position.x + forwardVec.x * 12.0f, drawEnt.position.y + forwardVec.y * 12.0f, drawEnt.position.z + forwardVec.z * 12.0f };
                    ImVec2 screenEnd;
                    if (WorldToScreen(endPos, screenEnd, vm)) {
                        draw->AddLine(screenPos, screenEnd, ImColor(255, 255, 0, 200), 2.5f);
                        draw->AddCircleFilled(screenEnd, 3.0f, ImColor(255, 255, 0, 255));
                    }
                }
            }

            if (settings::bLines) draw->AddLine(ImVec2(ScreenWidth / 2.0f, (float)ScreenHeight), ImVec2((mix + max) / 2.0f, may), boxColor);

            if (settings::bBox) {
                if (settings::bFilledBox) draw->AddRectFilled(ImVec2(mix, miy), ImVec2(max, may), ImColor(0, 0, 0, 80));
                draw->AddRect(ImVec2(mix - 1, miy - 1), ImVec2(max + 1, may + 1), ImColor(0, 0, 0), 0, 0, 3.0f);
                draw->AddRect(ImVec2(mix, miy), ImVec2(max, may), boxColor);
            }
            else {
                draw->AddCircle(screenPos, 6.0f, boxColor, 12, 2.0f);
            }

            if (settings::bBox3D && visibleCorners == 8) {
                draw->AddLine(s[0], s[1], boxColor); draw->AddLine(s[1], s[2], boxColor);
                draw->AddLine(s[2], s[3], boxColor); draw->AddLine(s[3], s[0], boxColor);
                draw->AddLine(s[4], s[5], boxColor); draw->AddLine(s[5], s[6], boxColor);
                draw->AddLine(s[6], s[7], boxColor); draw->AddLine(s[7], s[4], boxColor);
                draw->AddLine(s[0], s[4], boxColor); draw->AddLine(s[1], s[5], boxColor);
                draw->AddLine(s[2], s[6], boxColor); draw->AddLine(s[3], s[7], boxColor);
            }

            if (settings::bDistance && distMeters > 0.5f) {
                char dbuf[32];
                if (distMeters >= 1000.0f) {
                    _snprintf_s(dbuf, _TRUNCATE, "%.1fkm", distMeters / 1000.0f);
                }
                else {
                    _snprintf_s(dbuf, _TRUNCATE, "%.0fm", distMeters);
                }
                const ImVec2 textSize = ImGui::CalcTextSize(dbuf);
                const float textX = ((mix + max) * 0.5f) - (textSize.x * 0.5f);
                ImU32 distCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings::col_ESPText[0], settings::col_ESPText[1], settings::col_ESPText[2], settings::col_ESPText[3]));
                draw->AddText(ImVec2(textX, may + 2.0f), distCol, dbuf);
            }
            if (settings::bName) {
                ImU32 nameCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings::col_ESPText[0], settings::col_ESPText[1], settings::col_ESPText[2], settings::col_ESPText[3]));
                draw->AddText(ImVec2(mix, miy - 15), nameCol, ent.name.c_str());
            }

            if (settings::bReloadBar && !ent.isAir && ent.reloadProgress > 0.01f && ent.reloadProgress < 1.0f) {
                float w = max - mix;
                draw->AddRectFilled(ImVec2(mix, may + 16), ImVec2(max, may + 20), ImColor(50, 50, 50, 200));
                draw->AddRectFilled(ImVec2(mix, may + 16), ImVec2(mix + (w * ent.reloadProgress), may + 20), ImColor(219, 44, 44, 255));
                draw->AddRect(ImVec2(mix, may + 16), ImVec2(max, may + 20), ImColor(0, 0, 0));
            }
        }

        const float h = ent.bbMax.y - ent.bbMin.y;
        const Vector3 targetWeak = {
            (ent.bbMin.x + ent.bbMax.x) * 0.5f,
            ent.bbMin.y + (h * settings::targetHeightRatio),
            (ent.bbMin.z + ent.bbMax.z) * 0.5f
        };
        const Vector3 targetWorld = TransformVec(targetWeak, drawEnt.rotation, drawEnt.position);
        const Vector3 shooterPos = hasLocalUnit ? localUnitPos : lPos;

        const bool leadOn = settings::bPrediction || settings::bAirLead;
        const BallisticLead lead = SolveBallisticLead(
            shooterPos, targetWorld, ent.velocity, bVel, ent.isAir,
            leadOn, settings::bBulletDrop, settings::gravityScale);

        ImVec2 ps;
        if (ProjectToScreen(lead.aimPoint, ps, vm, vmAlt)) {
            const bool predictionActive = settings::bPrediction || settings::bBulletDrop || settings::bAirLead;

            // Draw ballistic prediction marker. Decoupled from full ESP so "弹道预测" in 战斗 tab is visible
            // even if ESP 总开关 is off. This makes the feature demonstrably work.
            if (predictionActive) {
                // Draw a distinct prediction/lead point + optional lead line from current pos
                ImVec2 curScreen;
                if (ProjectToScreen(drawEnt.position, curScreen, vm, vmAlt)) {
                    draw->AddLine(curScreen, ps, ImColor(255, 220, 50, 160), 1.0f);
                }
                if (ent.isAir) {
                    draw->AddCircle(ps, 9.0f, ImColor(255, 80, 80, 230), 12, 2.0f);
                    draw->AddCircleFilled(ps, 2.5f, ImColor(255, 100, 100, 255));
                } else {
                    draw->AddCircle(ps, 6.0f, ImColor(255, 220, 50, 220), 12, 1.5f);
                    draw->AddCircleFilled(ps, 2.0f, ImColor(255, 255, 80, 255));
                }
            } else if (settings::bEsp) {
                // Original behavior when only ESP (no explicit prediction) is on
                if (ent.isAir) {
                    draw->AddCircle(ps, 8.0f, ImColor(219, 44, 44, 255), 12, 2.0f);
                    draw->AddCircleFilled(ps, 2.0f, ImColor(219, 44, 44, 255));
                }
                else {
                    draw->AddCircleFilled(ps, 3.0f, ImColor(255, 255, 0));
                }
            }

            float cx = ps.x - (float)ScreenWidth / 2, cy = ps.y - (float)ScreenHeight / 2, cxDist = std::sqrt(cx * cx + cy * cy);
            if (!isTeammate && cxDist < minDist) { minDist = cxDist; bestTarget = ps; }
        }
    }

    if (settings::bShowFovCircle) {
        draw->AddCircle(ImVec2((float)ScreenWidth / 2.0f, (float)ScreenHeight / 2.0f), settings::aimFov, ImGui::ColorConvertFloat4ToU32(*(ImVec4*)settings::col_Fov), 64, 1.0f);
    }

    if (settings::bMemoryAim && bestTarget.x != 0 && (GetAsyncKeyState(settings::aimKey) & 0x8000)) {
        MoveMouse(bestTarget.x, bestTarget.y);
    }
}