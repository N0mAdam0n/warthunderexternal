#pragma once
#include <cmath>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

struct Vector2 {
    float x, y;
};

struct Vector3 {
    float x, y, z;
    Vector3 operator+(const Vector3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vector3 operator-(const Vector3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vector3 operator*(float s) const { return { x * s, y * s, z * s }; }
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    void Normalize() { float len = Length(); if (len > 0) { x /= len; y /= len; z /= len; } }
    float Distance(const Vector3& b) const {
        return std::sqrt((x - b.x) * (x - b.x) + (y - b.y) * (y - b.y) + (z - b.z) * (z - b.z));
    }
};

struct Matrix3x3 { float m[9]; };
struct Matrix4x4 { float m[16]; };

struct InternalPart {
    Vector3 position{ 0,0,0 };
    Vector3 bbMin{ 0,0,0 };
    Vector3 bbMax{ 0,0,0 };
    Matrix4x4 transform{ 0 };
    std::string name;

    std::vector<Vector3> vertices;
    std::vector<uint16_t> indices;
};

struct CachedEntity {
    std::vector<InternalPart> internals;
    uintptr_t pointer;
    Vector3 position;
    Vector3 velocity;
    Vector3 bbMin;
    Vector3 bbMax;
    Matrix3x3 rotation;
    std::string name;
    int team;
    float reloadProgress;
    bool isAir;
    bool isBot;
    bool isValid;

    CachedEntity() :
        pointer(0), position{ 0,0,0 }, velocity{ 0,0,0 }, bbMin{ 0,0,0 }, bbMax{ 0,0,0 },
        rotation{ 0 }, name(""), team(0), reloadProgress(0.0f), isAir(false), isBot(false), isValid(false) {
    }
};

// Maws rock struct
struct CachedRocket {
    uintptr_t pointer;
    Vector3 position;
    Vector3 velocity;
    std::string name;
    float distanceToLocal;
    float timeToImpact;
    bool isThreat;
};

namespace app {
    extern std::atomic<bool> running;
}

namespace shared {
    extern std::vector<CachedEntity> Entities;
    extern std::vector<CachedRocket> Rockets; 
    extern Matrix4x4 ViewMatrix;
    extern Matrix4x4 ViewMatrixAlt;
    extern Vector3 LocalPos;
    extern Vector3 LocalUnitPos;
    extern Vector3 CCIPPos;
    extern float LiveVelocity;  // initialized to 800 in main.cpp, updated from game ballistics via DMA
    extern int LocalTeam;
    extern std::mutex DataMutex;
    extern std::atomic<uintptr_t> TargetHijackPtr;
    extern std::atomic<uint64_t> viewGeneration;
    extern std::atomic<uint64_t> entityGeneration;
    extern std::atomic<uint64_t> entityCacheTick;
    extern std::atomic<uint32_t> cachedEntityCount;
    extern std::atomic<uint32_t> rawUnitCount;
    extern std::atomic<bool> gameLinkOk;
}

namespace perf {
    extern std::atomic<uint32_t> viewFps;
    extern std::atomic<uint32_t> entityFps;
    extern std::atomic<uint32_t> drawFps;
    extern std::atomic<uint32_t> loopFps;
    extern std::atomic<uint32_t> cacheMs;
}

namespace settings {
    extern bool bEulaAccepted;
    extern bool bStreamerMode;

    extern bool bMemoryAim;
    extern int aimKey;
    extern float aimFov;
    extern float aimSmooth;
    extern bool bShowFovCircle;
    extern bool bPrediction;
    extern bool bBulletDrop;
    extern float gravityScale;
    extern float targetHeightRatio;

    extern bool bEsp;
    extern bool bEspBots;
    extern bool bEspTeammates;
    extern bool bEspGround;
    extern bool bForceChineseNames;
    extern int espFontIndex;
    extern float espFontSize;
    extern bool bBox;
    extern bool bBox3D;
    extern bool bFilledBox;
    extern bool bLines;
    extern bool bName;
    extern bool bDistance;
    extern bool bReloadBar;
    extern bool bFacing;
    extern bool bRadar;
    extern bool bMissileESP; 

    extern bool bInternalsESP;
    extern int iInternalsMode;
    extern bool bInternalsName;

    extern bool bCCIP;
    extern bool bAirLead;
    extern bool bDangerWarning;

    extern bool bAutoTeam;
    extern int ManualTeam;

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

    extern std::atomic<bool> bMindControlActive;

    extern float col_BoxVis[4];
    extern float col_BoxTeam[4];
    extern float col_Fov[4];
    extern float col_ESPText[4];
    extern int espMarkerStyle;
}