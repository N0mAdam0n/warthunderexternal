#include "structs.hpp"
#include "memory.hpp"
#include "input.hpp"
#include "imgui/imgui.h"
#include <cmath>

extern int ScreenWidth;
extern int ScreenHeight;

bool WorldToScreen(const Vector3& pos, ImVec2& out, const Matrix4x4& vm) {
    float cw = pos.x * vm.m[3] + pos.y * vm.m[7] + pos.z * vm.m[11] + vm.m[15]; if (cw < 0.1f) return false;
    float inv_w = 1.0f / cw;
    out.x = ((pos.x * vm.m[0] + pos.y * vm.m[4] + pos.z * vm.m[8] + vm.m[12]) * inv_w * 0.5f + 0.5f) * (float)ScreenWidth;
    out.y = (0.5f - (pos.x * vm.m[1] + pos.y * vm.m[5] + pos.z * vm.m[9] + vm.m[13]) * inv_w * 0.5f) * (float)ScreenHeight;
    return true;
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

static Vector3 PredictEntityPos(const CachedEntity& ent) {
    const uint64_t cacheTick = shared::entityCacheTick.load(std::memory_order_relaxed);
    if (cacheTick == 0) return ent.position;

    float ageSec = (GetTickCount64() - cacheTick) / 1000.0f;
    if (ageSec < 0.0f) ageSec = 0.0f;
    if (ageSec > 0.12f) ageSec = 0.12f;

    return ent.position + ent.velocity * ageSec;
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

void RenderESP(ImDrawList* draw) {
    std::vector<CachedEntity> localEntities;
    std::vector<CachedRocket> localRockets;
    Matrix4x4 vm; Vector3 lPos, ccip; float bVel; int lTeam;

    {
        std::lock_guard<std::mutex> lock(shared::DataMutex);
        localEntities = shared::Entities;
        localRockets = shared::Rockets; // Get synced missiles
        vm = shared::ViewMatrix; lPos = shared::LocalPos;
        ccip = shared::CCIPPos; bVel = shared::LiveVelocity; lTeam = shared::LocalTeam;
    }

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

                char rText[64];
                sprintf_s(rText, "%s[%.1fkm]", r.name.c_str(), r.distanceToLocal / 1000.0f);
                draw->AddText(ImVec2(sPos.x + 10, sPos.y - 8), ImColor(255, 100, 100), rText);

                if (r.isThreat) {
                    char warn[64];
                    sprintf_s(warn, "MISSILE INCOMING: %.1fs", r.timeToImpact);
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
            draw->AddText(ImVec2(screenCCIP.x + 8, screenCCIP.y - 8), ImColor(255, 50, 50, 255), "CCIP");
        }
    }

    ImVec2 bestTarget = { 0,0 }; float minDist = settings::aimFov;

    for (const auto& ent : localEntities) {
        if (settings::bAutoTeam) { if (ent.team == lTeam) continue; }
        else { if (ent.team == settings::ManualTeam) continue; }

        CachedEntity drawEnt = ent;
        drawEnt.position = PredictEntityPos(ent);

        ImVec2 screenPos;
        if (!WorldToScreen(drawEnt.position, screenPos, vm)) continue;

        float dist = drawEnt.position.x * vm.m[3] + drawEnt.position.y * vm.m[7] + drawEnt.position.z * vm.m[11] + vm.m[15];
        if (dist < 0.0f) dist = 0.0f;

        ImU32 boxColor = ImGui::ColorConvertFloat4ToU32(*(ImVec4*)settings::col_BoxVis);

        Vector3 c[8] = { {ent.bbMin.x, ent.bbMin.y, ent.bbMin.z}, {ent.bbMax.x, ent.bbMin.y, ent.bbMin.z}, {ent.bbMax.x, ent.bbMax.y, ent.bbMin.z}, {ent.bbMin.x, ent.bbMax.y, ent.bbMin.z}, {ent.bbMin.x, ent.bbMin.y, ent.bbMax.z}, {ent.bbMax.x, ent.bbMin.y, ent.bbMax.z}, {ent.bbMax.x, ent.bbMax.y, ent.bbMax.z}, {ent.bbMin.x, ent.bbMax.y, ent.bbMax.z} };
        ImVec2 s[8]; bool validBox = true; float mix = 10000, max = -10000, miy = 10000, may = -10000;
        for (int k = 0; k < 8; k++) {
            if (!WorldToScreen(TransformVec(c[k], drawEnt.rotation, drawEnt.position), s[k], vm)) { validBox = false; break; }
            if (s[k].x < mix) mix = s[k].x; if (s[k].x > max) max = s[k].x;
            if (s[k].y < miy) miy = s[k].y; if (s[k].y > may) may = s[k].y;
        }

        if (settings::bDangerWarning && IsAimingAtMe(drawEnt.position, lPos, drawEnt.rotation)) {
            boxColor = ImColor(255, 0, 0);
            ImVec2 cent((float)ScreenWidth / 2.0f, (float)ScreenHeight); ImVec2 es;
            if (WorldToScreen(drawEnt.position, es, vm)) draw->AddLine(cent, es, ImColor(255, 0, 0, 150), 2.0f);
        }

        if (settings::bEsp && validBox) {

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

            if (settings::bBox3D) {
                draw->AddLine(s[0], s[1], boxColor); draw->AddLine(s[1], s[2], boxColor);
                draw->AddLine(s[2], s[3], boxColor); draw->AddLine(s[3], s[0], boxColor);
                draw->AddLine(s[4], s[5], boxColor); draw->AddLine(s[5], s[6], boxColor);
                draw->AddLine(s[6], s[7], boxColor); draw->AddLine(s[7], s[4], boxColor);
                draw->AddLine(s[0], s[4], boxColor); draw->AddLine(s[1], s[5], boxColor);
                draw->AddLine(s[2], s[6], boxColor); draw->AddLine(s[3], s[7], boxColor);
            }

            if (settings::bDistance) {
                char dbuf[32]; sprintf_s(dbuf, "%.0fm", dist);
                draw->AddText(ImVec2(mix, may + 2), ImColor(255, 255, 255), dbuf);
            }
            if (settings::bName) draw->AddText(ImVec2(mix, miy - 15), ImColor(200, 200, 200), ent.name.c_str());

            if (settings::bReloadBar && !ent.isAir && ent.reloadProgress > 0.01f && ent.reloadProgress < 1.0f) {
                float w = max - mix;
                draw->AddRectFilled(ImVec2(mix, may + 16), ImVec2(max, may + 20), ImColor(50, 50, 50, 200));
                draw->AddRectFilled(ImVec2(mix, may + 16), ImVec2(mix + (w * ent.reloadProgress), may + 20), ImColor(219, 44, 44, 255));
                draw->AddRect(ImVec2(mix, may + 16), ImVec2(max, may + 20), ImColor(0, 0, 0));
            }
        }

        // prediction math
        float h = ent.bbMax.y - ent.bbMin.y;
        Vector3 targetWeak = { (ent.bbMin.x + ent.bbMax.x) / 2.0f, ent.bbMin.y + (h * settings::targetHeightRatio), (ent.bbMin.z + ent.bbMax.z) / 2.0f };
        Vector3 tReal = TransformVec(targetWeak, drawEnt.rotation, drawEnt.position);

        float fTime = bVel > 10.0f ? dist / bVel : 0.0f;

        if (settings::bPrediction || settings::bAirLead) {
            tReal = tReal + (ent.velocity * fTime);
        }

        if (settings::bBulletDrop && bVel > 10.0f) {
            tReal.y += 0.5f * (9.81f * settings::gravityScale) * (fTime * fTime);
        }

        ImVec2 ps;
        if (WorldToScreen(tReal, ps, vm)) {
            if (settings::bEsp) {
                if (ent.isAir) {
                    draw->AddCircle(ps, 8.0f, ImColor(219, 44, 44, 255), 12, 2.0f);
                    draw->AddCircleFilled(ps, 2.0f, ImColor(219, 44, 44, 255));
                }
                else {
                    draw->AddCircleFilled(ps, 3.0f, ImColor(255, 255, 0));
                }
            }
            float cx = ps.x - (float)ScreenWidth / 2, cy = ps.y - (float)ScreenHeight / 2, cxDist = std::sqrt(cx * cx + cy * cy);
            if (cxDist < minDist) { minDist = cxDist; bestTarget = ps; }
        }
    }

    if (settings::bShowFovCircle) {
        ImGui::GetBackgroundDrawList()->AddCircle(ImVec2((float)ScreenWidth / 2.0f, (float)ScreenHeight / 2.0f), settings::aimFov, ImGui::ColorConvertFloat4ToU32(*(ImVec4*)settings::col_Fov), 64, 1.0f);
    }

    if (settings::bMemoryAim && bestTarget.x != 0 && (GetAsyncKeyState(settings::aimKey) & 0x8000)) {
        MoveMouse(bestTarget.x, bestTarget.y);
    }
}