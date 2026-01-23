#define IMGUI_DEFINE_MATH_OPERATORS
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NO_STRICT

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <winsock2.h>
#include <psapi.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <intrin.h>
#include <fstream>
#include <sstream>
#include <map>
#include <random>

#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_dx9.h"
#include "Imgui/imgui_impl_win32.h"
#include "Imgui/imgui.cpp"
#include "Imgui/imgui_draw.cpp"
#include "Imgui/imgui_tables.cpp"
#include "Imgui/imgui_widgets.cpp"
#include "Imgui/imgui_impl_dx9.cpp"
#include "Imgui/imgui_impl_win32.cpp"

#include "minhook/MinHook.h"
#pragma comment(lib, "minhook/MinHook.x86.lib")
#pragma comment(lib, "psapi.lib")

#include <sampapi/sampapi.h>
#include <sampapi/CVector.h>
#include <sampapi/CMatrix.h>
#include <sampapi/0.3.7-R3-1/CNetGame.h>
#include <sampapi/0.3.7-R3-1/CPlayerPool.h>
#include <sampapi/0.3.7-R3-1/CRemotePlayer.h>
#include <sampapi/0.3.7-R3-1/CPlayerInfo.h>
#include <sampapi/0.3.7-R3-1/CLocalPlayer.h>
#include <sampapi/0.3.7-R3-1/CPed.h>
#include <sampapi/0.3.7-R3-1/CVehiclePool.h>
#include <sampapi/0.3.7-R3-1/CVehicle.h>
#include <sampapi/0.3.7-R3-1/CCamera.h>
#include <sampapi/0.3.7-R3-1/CGame.h>
#include <sampapi/0.3.7-R3-1/AimStuff.h>

namespace samp = sampapi::v037r3;

typedef HRESULT(STDMETHODCALLTYPE* EndScene_t)(IDirect3DDevice9*);
typedef HRESULT(STDMETHODCALLTYPE* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef BOOL(WINAPI* SetCursorPos_t)(int, int);

EndScene_t Original_EndScene = nullptr;
Reset_t Original_Reset = nullptr;
WNDPROC Original_WndProc = nullptr;
SetCursorPos_t Original_SetCursorPos = nullptr;

bool init = false;
bool show_menu = false;
HWND game_hwnd = nullptr;
sampapi::CMatrix localMat;

// ============================================
// ОСНОВНЫЕ НАСТРОЙКИ
// ============================================

// ESP Settings
bool espEnabled = false;
bool espNames = false;
bool espHealthArmor = false;
bool espLines = false;
bool espDistance = false;
bool espWeapon = false;
bool espSkeleton = false;
bool espBox = false;
bool vehicleEspEnabled = false;

// Aimbot Settings
bool aimbotEnabled = false;
bool aimbotKeyEnabled = true;
bool aimbotPrediction = false;
bool aimbotVisibilityCheck = false;
float aimbotFovRadius = 150.0f;
float aimbotRange = 100.0f;
float aimbotSmooth = 2.5f;
float aimbotPredictionTime = 0.1f;

// Calibration
float aimOffsetX = 40.0f;
float aimOffsetY = -45.0f;

// Weapon Hacks
bool autoShootEnabled = false;
bool autoShootKeyEnabled = false;
bool rapidFireEnabled = false;
bool noRecoilEnabled = false;
bool noSpreadEnabled = false;
bool noReloadEnabled = false;
bool triggerBotEnabled = false;
float triggerDelay = 50.0f;

// Movement & GodMode
bool godModeEnabled = false;
bool airBreakEnabled = false;
bool firstActivation = true;
bool speedHackEnabled = false;
float speedMultiplier = 2.0f;
bool slapperEnabled = false;
float slapperForce = 150.0f;
float slapperRadius = 15.0f;
int slapperLagInterval = 200;

// Visuals & Radar
bool radarEnabled = false;
float radarZoom = 100.0f;
bool customCrosshairEnabled = false;
int crosshairStyle = 0;
ImVec4 crosshairColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
float crosshairSize = 10.0f;

// Safety Settings
struct ESPSettings {
    bool onlyInRange = true;
    float maxDistance = 50.0f;
    bool hideInInterior = true;
    bool hideAdmins = true;
} espSettings;

struct AimbotSafety {
    float reactionTime = 150.0f;
    float maxSpeed = 8.0f;
    bool addHumanError = true;
    float errorAmount = 2.0f;
} aimbotSafety;

// ============================================
// СТАТИСТИКА
// ============================================
struct Statistics {
    int totalShots = 0;
    int hits = 0;
    int headshots = 0;
    DWORD sessionStart = 0;
    bool showStats = false;

    void Start() { sessionStart = GetTickCount(); }

    float GetAccuracy() {
        return totalShots > 0 ? (hits * 100.0f / totalShots) : 0.0f;
    }

    float GetHeadshotRate() {
        return hits > 0 ? (headshots * 100.0f / hits) : 0.0f;
    }

    void DrawStats() {
        if (!showStats) return;

        ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_FirstUseEver);
        ImGui::Begin("Statistics", &showStats, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Session: %d min", (GetTickCount() - sessionStart) / 60000);
        ImGui::Separator();
        ImGui::Text("Shots: %d", totalShots);
        ImGui::Text("Hits: %d", hits);
        ImGui::Text("Headshots: %d", headshots);
        ImGui::Text("Accuracy: %.1f%%", GetAccuracy());
        ImGui::Text("Headshot Rate: %.1f%%", GetHeadshotRate());
        ImGui::End();
    }
};

Statistics stats;

// ============================================
// PANIC MODE
// ============================================
class PanicMode {
private:
    bool isPanicActive = false;
    bool saved_aimbot, saved_esp, saved_godmode, saved_speedhack;
    bool saved_airbreak, saved_triggerbot, saved_radar;

public:
    void Activate() {
        if (isPanicActive) return;

        saved_aimbot = aimbotEnabled;
        saved_esp = espEnabled;
        saved_godmode = godModeEnabled;
        saved_speedhack = speedHackEnabled;
        saved_airbreak = airBreakEnabled;
        saved_triggerbot = triggerBotEnabled;
        saved_radar = radarEnabled;

        aimbotEnabled = false;
        espEnabled = false;
        godModeEnabled = false;
        speedHackEnabled = false;
        airBreakEnabled = false;
        triggerBotEnabled = false;
        radarEnabled = false;
        vehicleEspEnabled = false;
        espSkeleton = false;
        espBox = false;
        show_menu = false;

        isPanicActive = true;
    }

    void Deactivate() {
        if (!isPanicActive) return;

        aimbotEnabled = saved_aimbot;
        espEnabled = saved_esp;
        godModeEnabled = saved_godmode;
        speedHackEnabled = saved_speedhack;
        airBreakEnabled = saved_airbreak;
        triggerBotEnabled = saved_triggerbot;
        radarEnabled = saved_radar;

        isPanicActive = false;
    }

    bool IsActive() { return isPanicActive; }

    void DrawIndicator() {
        if (!isPanicActive) return;

        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(250, 80));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.8f, 0, 0, 0.9f));
        ImGui::Begin("##PANIC", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        ImGui::SetCursorPosX(50);
        ImGui::TextColored(ImVec4(1, 1, 1, 1), "PANIC MODE ACTIVE");
        ImGui::Separator();
        ImGui::Text("All features disabled");
        ImGui::Text("Press END to restore");

        ImGui::End();
        ImGui::PopStyleColor();
    }
};

PanicMode panicMode;

// ============================================
// ADMIN DETECTOR
// ============================================
class AdminDetector {
private:
    struct SuspiciousPlayer {
        std::string name;
        DWORD firstSeenTime;
        int suspicionLevel;
    };

    std::vector<SuspiciousPlayer> suspiciousList;

public:
    bool showWarnings = true;

    bool IsShowWarningsEnabled() const {
        return showWarnings;
    }

    void SetShowWarnings(bool value) {
        showWarnings = value;
    }

    bool IsAdmin(const char* name) {
        if (!name) return false; 
        const char* adminPrefixes[] = {
            "[A]", "[M]", "[KR]", "[Hlp]", "[CKA]", "[MLD]", "[SK]"
        };

        for (auto& prefix : adminPrefixes) {
            if (strstr(name, prefix)) return true;
        }
        return false;
    }

    void AnalyzePlayer(const char* name, samp::CPed* player) {
        if (!player || !player->m_pGamePed) return;

        int suspicion = 0;

        BYTE alpha = *(BYTE*)((uintptr_t)player->m_pGamePed + 0x4C8);
        if (alpha < 50) suspicion += 30;

        float health = *(float*)((uintptr_t)player->m_pGamePed + 0x540);
        if (health > 200.0f) suspicion += 50;

        if (IsAdmin(name)) suspicion += 100;

        if (suspicion > 50) {
            bool found = false;
            for (auto& sus : suspiciousList) {
                if (sus.name == name) {
                    sus.suspicionLevel = suspicion;
                    found = true;
                    break;
                }
            }

            if (!found) {
                suspiciousList.push_back({ name, GetTickCount(), suspicion });
            }
        }
    }

    void DrawWarnings() {
        if (!showWarnings || suspiciousList.empty()) return;

        ImGui::SetNextWindowPos(ImVec2(10, 300), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

        ImGui::Begin("Admin Detection", &showWarnings);

        ImGui::TextColored(ImVec4(1, 0, 0, 1), "POSSIBLE ADMINS NEARBY:");
        ImGui::Separator();

        DWORD currentTime = GetTickCount();
        for (auto& sus : suspiciousList) {
            DWORD timeSeen = (currentTime - sus.firstSeenTime) / 1000;

            ImVec4 color;
            if (sus.suspicionLevel >= 100) color = ImVec4(1, 0, 0, 1);
            else if (sus.suspicionLevel >= 50) color = ImVec4(1, 0.5f, 0, 1);
            else color = ImVec4(1, 1, 0, 1);

            ImGui::TextColored(color, "%s [%d%%] (%ds)",
                sus.name.c_str(), sus.suspicionLevel, timeSeen);
        }

        if (ImGui::Button("Clear List")) {
            suspiciousList.clear();
        }

        ImGui::End();
    }

    void CleanupOldEntries() {
        DWORD currentTime = GetTickCount();
        suspiciousList.erase(
            std::remove_if(suspiciousList.begin(), suspiciousList.end(),
                [currentTime](const SuspiciousPlayer& sp) {
                    return (currentTime - sp.firstSeenTime) > 300000;
                }),
            suspiciousList.end()
        );
    }

    bool HasSuspiciousPlayers() { return !suspiciousList.empty(); }
};

AdminDetector adminDetector;

// ============================================
// УТИЛИТЫ
// ============================================

BOOL WINAPI Hooked_SetCursorPos(int X, int Y)
{
    if (show_menu) return TRUE;
    return Original_SetCursorPos(X, Y);
}

extern LRESULT __cdecl ImGui_ImplWin32_WndProcHandler(void* hwnd, unsigned int msg, unsigned int wparam, long lparam);

LRESULT CALLBACK Hooked_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (show_menu)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return TRUE;

        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse || io.WantCaptureKeyboard)
            return TRUE;
    }

    return CallWindowProc((FARPROC)Original_WndProc, hWnd, msg, wParam, lParam);
}

HRESULT STDMETHODCALLTYPE Hooked_Reset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
    if (!init) return Original_Reset(pDevice, pPresentationParameters);

    ImGui_ImplDX9_InvalidateDeviceObjects();

    HRESULT hr = Original_Reset(pDevice, pPresentationParameters);

    if (SUCCEEDED(hr))
    {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return hr;
}

samp::CPed* GetLocalPlayer()
{
    auto netGame = samp::RefNetGame();
    if (!netGame || !netGame->m_pPools || !netGame->m_pPools->m_pPlayer) return nullptr;

    auto playerPool = netGame->m_pPools->m_pPlayer;

    if (!playerPool->m_localInfo.m_pObject || !playerPool->m_localInfo.m_pObject->m_pPed)
        return nullptr;

    return playerPool->m_localInfo.m_pObject->m_pPed;
}

bool WorldToScreen(const sampapi::CVector& worldPos, ImVec2& screenPos)
{
    typedef bool(__cdecl* CalcScreenCoors_t)(const sampapi::CVector&, sampapi::CVector*, float*, float*, bool, bool);
    static CalcScreenCoors_t CalcScreenCoors = (CalcScreenCoors_t)0x70CE30;

    sampapi::CVector out;
    float fW, fH;
    if (CalcScreenCoors(worldPos, &out, &fW, &fH, true, true))
    {
        screenPos.x = out.x;
        screenPos.y = out.y;
        return true;
    }
    return false;
}

const char* GetWeaponName(int weaponId) {
    static const char* weaponNames[] = {
        "Fist", "Brass Knuckles", "Golf Club", "Nightstick", "Knife",
        "Baseball Bat", "Shovel", "Pool Cue", "Katana", "Chainsaw",
        "Purple Dildo", "Dildo", "Vibrator", "Silver Vibrator", "Flowers",
        "Cane", "Grenade", "Tear Gas", "Molotov", "Unknown", "Unknown",
        "Unknown", "9mm", "Silenced 9mm", "Desert Eagle", "Shotgun",
        "Sawnoff Shotgun", "Combat Shotgun", "Micro SMG", "MP5", "AK-47",
        "M4", "Tec-9", "Country Rifle", "Sniper Rifle", "RPG",
        "HS Rocket", "Flamethrower", "Minigun", "Satchel Charge", "Detonator",
        "Spraycan", "Fire Extinguisher", "Camera", "Night Vis", "Thermal",
        "Parachute"
    };

    if (weaponId >= 0 && weaponId < 47) return weaponNames[weaponId];
    return "Unknown";
}

// ============================================
// ESP ФУНКЦИИ
// ============================================

void DrawSkeletonESP(samp::CPed* player) {
    if (!player || !player->m_pGamePed) return;

    struct BoneConnection { int bone1, bone2; };
    static BoneConnection skeleton[] = {
        {3, 51}, {51, 52}, {52, 42}, {42, 43}, {43, 44},
        {52, 32}, {32, 33}, {33, 34},
        {3, 21}, {21, 22}, {22, 23},
        {3, 31}, {31, 32}, {32, 33},
        {3, 2}
    };

    auto drawList = ImGui::GetBackgroundDrawList();

    for (auto& connection : skeleton) {
        sampapi::CVector bone1Pos, bone2Pos;
        player->GetBonePosition(connection.bone1, &bone1Pos);
        player->GetBonePosition(connection.bone2, &bone2Pos);

        ImVec2 screen1, screen2;
        if (WorldToScreen(bone1Pos, screen1) && WorldToScreen(bone2Pos, screen2)) {
            drawList->AddLine(screen1, screen2, IM_COL32(255, 255, 255, 200), 2.0f);
        }
    }
}

void DrawBoxESP(samp::CPed* player) {
    if (!player || !player->m_pGamePed) return;

    sampapi::CMatrix mat;
    player->GetMatrix(&mat);

    float height = 1.8f;
    float width = 0.5f;

    sampapi::CVector corners[8] = {
        {mat.pos.x - width, mat.pos.y - width, mat.pos.z},
        {mat.pos.x + width, mat.pos.y - width, mat.pos.z},
        {mat.pos.x + width, mat.pos.y + width, mat.pos.z},
        {mat.pos.x - width, mat.pos.y + width, mat.pos.z},
        {mat.pos.x - width, mat.pos.y - width, mat.pos.z + height},
        {mat.pos.x + width, mat.pos.y - width, mat.pos.z + height},
        {mat.pos.x + width, mat.pos.y + width, mat.pos.z + height},
        {mat.pos.x - width, mat.pos.y + width, mat.pos.z + height}
    };

    ImVec2 screenCorners[8];
    bool allVisible = true;

    for (int i = 0; i < 8; i++) {
        if (!WorldToScreen(corners[i], screenCorners[i])) {
            allVisible = false;
            break;
        }
    }

    if (!allVisible) return;

    auto drawList = ImGui::GetBackgroundDrawList();
    ImU32 color = IM_COL32(255, 0, 0, 200);

    drawList->AddLine(screenCorners[0], screenCorners[1], color, 2.0f);
    drawList->AddLine(screenCorners[1], screenCorners[2], color, 2.0f);
    drawList->AddLine(screenCorners[2], screenCorners[3], color, 2.0f);
    drawList->AddLine(screenCorners[3], screenCorners[0], color, 2.0f);

    drawList->AddLine(screenCorners[4], screenCorners[5], color, 2.0f);
    drawList->AddLine(screenCorners[5], screenCorners[6], color, 2.0f);
    drawList->AddLine(screenCorners[6], screenCorners[7], color, 2.0f);
    drawList->AddLine(screenCorners[7], screenCorners[4], color, 2.0f);

    // Vertical
    for (int i = 0; i < 4; i++) {
        drawList->AddLine(screenCorners[i], screenCorners[i + 4], color, 2.0f);
    }
}

void DrawPlayerESP(samp::CPed* localPlayer, samp::CPed* player, int screenWidth, int screenHeight)
{
    if (!player || !player->m_pGamePed || !localPlayer) return;

    auto pNetGame = samp::RefNetGame();
    if (!pNetGame || !pNetGame->m_pPools || !pNetGame->m_pPools->m_pPlayer) return;

    int playerId = pNetGame->m_pPools->m_pPlayer->Find(player->m_pGamePed);
    if (playerId == -1) return;

    const char* name = pNetGame->m_pPools->m_pPlayer->GetName(playerId);

    if (espSettings.hideAdmins && adminDetector.IsAdmin(name)) return;

    sampapi::CMatrix localMat, targetMat;
    localPlayer->GetMatrix(&localMat);
    player->GetMatrix(&targetMat);

    float dx = targetMat.pos.x - localMat.pos.x;
    float dy = targetMat.pos.y - localMat.pos.y;
    float dz = targetMat.pos.z - localMat.pos.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);

    if (espSettings.onlyInRange && dist > espSettings.maxDistance) return;

    if (espSettings.hideInInterior) {
        BYTE localInterior = *(BYTE*)((uintptr_t)localPlayer->m_pGamePed + 0x5C);
        BYTE targetInterior = *(BYTE*)((uintptr_t)player->m_pGamePed + 0x5C);
        if (localInterior != targetInterior) return;
    }

    sampapi::CVector headPos;
    player->GetBonePosition(8, &headPos);
    headPos.z += 0.2f;

    ImVec2 screenPos;
    if (WorldToScreen(headPos, screenPos))
    {
        auto drawList = ImGui::GetBackgroundDrawList();
        float health = *(float*)((DWORD)player->m_pGamePed + 0x540);

        if (espLines) {
            drawList->AddLine(
                ImVec2((float)screenWidth / 2.0f, (float)screenHeight),
                screenPos,
                IM_COL32(255, 255, 0, 120)
            );
        }

        if (espNames) {
            ImVec2 textSize = ImGui::CalcTextSize(name);
            ImVec2 textPos = ImVec2(screenPos.x - (textSize.x / 2.0f), screenPos.y - 15.0f);
            drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), name);
        }

        if (espHealthArmor) {
            float barWidth = 35.0f;
            float healthRatio = (health > 100.0f ? 100.0f : (health < 0.0f ? 0.0f : health)) / 100.0f;
            ImVec2 barPos = ImVec2(screenPos.x - barWidth / 2.0f, screenPos.y - 2.0f);
            drawList->AddRectFilled(barPos, ImVec2(barPos.x + barWidth, barPos.y + 4), IM_COL32(0, 0, 0, 200));
            drawList->AddRectFilled(barPos, ImVec2(barPos.x + (barWidth * healthRatio), barPos.y + 4), IM_COL32(0, 255, 0, 255));
        }

        if (espWeapon) {
            int weaponId = *(int*)((uintptr_t)player->m_pGamePed + 0x718);
            const char* weaponName = GetWeaponName(weaponId);
            ImVec2 textSize = ImGui::CalcTextSize(weaponName);
            drawList->AddText(
                ImVec2(screenPos.x - textSize.x / 2, screenPos.y + 10),
                IM_COL32(255, 165, 0, 255),
                weaponName
            );
        }

        if (espDistance) {
            char distText[32];
            sprintf(distText, "%.1fm", dist);
            ImVec2 textSize = ImGui::CalcTextSize(distText);
            drawList->AddText(
                ImVec2(screenPos.x - textSize.x / 2, screenPos.y + (espWeapon ? 25 : 10)),
                IM_COL32(255, 255, 0, 255),
                distText
            );
        }
    }

    if (espSkeleton) DrawSkeletonESP(player);
    if (espBox) DrawBoxESP(player);
}

void DrawVehicleESP(int screenWidth, int screenHeight) {
    auto pNetGame = samp::RefNetGame();
    if (!pNetGame || !pNetGame->m_pPools || !pNetGame->m_pPools->m_pVehicle) return;

    auto drawList = ImGui::GetBackgroundDrawList();
    auto pVehiclePool = pNetGame->m_pPools->m_pVehicle;

    for (int i = 0; i < 2000; i++) {
        void* pSAMPVehicle = (void*)pVehiclePool->m_pObject[i];
        if (!pSAMPVehicle) continue;

        uintptr_t pGameVeh = *(uintptr_t*)((uintptr_t)pSAMPVehicle + 0x40);
        if (!pGameVeh) continue;

        uintptr_t pMatrix = *(uintptr_t*)(pGameVeh + 0x14);
        if (!pMatrix) continue;

        sampapi::CVector vehPos;
        vehPos.x = *(float*)(pMatrix + 0x30);
        vehPos.y = *(float*)(pMatrix + 0x34);
        vehPos.z = *(float*)(pMatrix + 0x38);
        vehPos.z += 1.0f;

        ImVec2 screenPos;
        if (WorldToScreen(vehPos, screenPos)) {
            float health = *(float*)(pGameVeh + 0x4C0);
            char szVehInfo[64];
            sprintf(szVehInfo, "Veh ID: %d | HP: %.0f", i, health);
            ImVec2 textSize = ImGui::CalcTextSize(szVehInfo);
            drawList->AddText(ImVec2(screenPos.x - textSize.x / 2.0f, screenPos.y),
                IM_COL32(0, 255, 255, 255), szVehInfo);
        }
    }
}

// ============================================
// SMART AIMBOT - ЗАВЕРШЕННАЯ ВЕРСИЯ
// ============================================

class SmartAimbot {
private:
    struct MouseMovement {
        float dx, dy;
        DWORD timestamp;
    };
    std::vector<MouseMovement> movementHistory;
    DWORD lastAimTime = 0;
    ImVec2 lastTargetPos = { 0, 0 };
    std::random_device rd;
    std::mt19937 gen;
    std::uniform_real_distribution<float> errorDist;
    public:
        samp::CPed* FindBestTarget(samp::CPed* localPlayer, std::vector<samp::CPed*>& players,
            float crosshairX, float crosshairY) {
            if (!localPlayer || players.empty()) return nullptr;

            float closestFov = aimbotFovRadius;
            samp::CPed* bestTarget = nullptr;

            sampapi::CMatrix localMat;
            localPlayer->GetMatrix(&localMat);

            for (auto player : players) {
                if (!player || player->IsDead()) continue;

                sampapi::CMatrix targetMat;
                player->GetMatrix(&targetMat);

                float dx = targetMat.pos.x - localMat.pos.x;
                float dy = targetMat.pos.y - localMat.pos.y;
                float dz = targetMat.pos.z - localMat.pos.z;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);

                if (dist > aimbotRange) continue;

                sampapi::CVector bonePos;
                player->GetBonePosition(3, &bonePos);

                ImVec2 screenPos;
                if (WorldToScreen(bonePos, screenPos)) {
                    float deltaX = screenPos.x - crosshairX;
                    float deltaY = screenPos.y - crosshairY;
                    float fovDist = sqrtf(deltaX * deltaX + deltaY * deltaY);

                    if (fovDist < closestFov) {
                        closestFov = fovDist;
                        bestTarget = player;
                    }
                }
            }

            return bestTarget;
        }

        void AimAt(samp::CPed* target, float crosshairX, float crosshairY) {
            if (!target) return;

            DWORD currentTime = GetTickCount();

            if (currentTime - lastAimTime < aimbotSafety.reactionTime) return;

            sampapi::CVector targetPos;
            target->GetBonePosition(3, &targetPos);

            if (aimbotPrediction) {
                sampapi::CVector velocity;
                target->GetSpeed(&velocity);
                targetPos.x += velocity.x * aimbotPredictionTime;
                targetPos.y += velocity.y * aimbotPredictionTime;
                targetPos.z += velocity.z * aimbotPredictionTime;
            }

            ImVec2 screenPos;
            if (!WorldToScreen(targetPos, screenPos)) return;

            float diffX = screenPos.x - crosshairX;
            float diffY = screenPos.y - crosshairY;

            if (aimbotSafety.addHumanError) {
                float errorX = ((rand() % 100) / 100.0f - 0.5f) * aimbotSafety.errorAmount;
                float errorY = ((rand() % 100) / 100.0f - 0.5f) * aimbotSafety.errorAmount;
                diffX += errorX;
                diffY += errorY;
            }

            diffX /= aimbotSmooth;
            diffY /= aimbotSmooth;

            float moveSpeed = sqrtf(diffX * diffX + diffY * diffY);
            if (moveSpeed > aimbotSafety.maxSpeed) {
                float ratio = aimbotSafety.maxSpeed / moveSpeed;
                diffX *= ratio;
                diffY *= ratio;
            }

            mouse_event(MOUSEEVENTF_MOVE, (DWORD)diffX, (DWORD)diffY, 0, 0);

            lastAimTime = currentTime;
            lastTargetPos = screenPos;

            samp::AimStuff::UpdateAim();
        }
    };

    SmartAimbot smartAimbot;

    // ============================================
    // WEAPON FUNCTIONS
    // ============================================
    void ApplyNoRecoil() {
        if (!noRecoilEnabled) return;
        samp::AimStuff::UpdateAim();
    }

    void ApplyNoSpread() {
        if (!noSpreadEnabled) return;
        auto pGame = samp::RefGame();
        if (!pGame || !pGame->m_pPlayerPed) return;

        auto pInfo = pGame->m_pPlayerPed->GetCurrentWeaponInfo();
        if (pInfo) {
            *(float*)((uintptr_t)pInfo + 0x20) = 100.0f;
            *(float*)((uintptr_t)pInfo + 0x24) = 100.0f;
        }
    }

    void ApplyRapidFire() {
        if (!rapidFireEnabled) return;
        auto pGame = samp::RefGame();
        if (!pGame || !pGame->m_pPlayerPed) return;

        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
            uintptr_t pPed = (uintptr_t)pGame->m_pPlayerPed->m_pGamePed;
            if (pPed) {
                *(DWORD*)(pPed + 0x560) = 0;
            }
        }
    }

    void ApplyNoReload() {
        if (!noReloadEnabled) return;
        auto pGame = samp::RefGame();
        if (!pGame || !pGame->m_pPlayerPed) return;

        int weaponId = pGame->m_pPlayerPed->GetCurrentWeapon();
        if (weaponId < 22) return;

        auto pWeapon = pGame->m_pPlayerPed->GetWeaponSlot(weaponId);
        if (pWeapon) {
            *(int*)((uintptr_t)pWeapon + 0x08) = 50;
        }
    }

    // ============================================
    // MOVEMENT FUNCTIONS
    // ============================================
    void AirBreak(samp::CPed* localPlayer) {
        if (!airBreakEnabled) {
            firstActivation = true;
            return;
        }

        static sampapi::CVector pos;

        if (firstActivation) {
            if (localPlayer) {
                localPlayer->GetMatrix(&localMat);
                pos = localMat.pos;
                firstActivation = false;
            }
        }

        float speed = 0.5f;
        if (GetAsyncKeyState(VK_SHIFT)) speed *= 3.0f;

        if (GetAsyncKeyState('W')) pos.y += speed;
        if (GetAsyncKeyState('S')) pos.y -= speed;
        if (GetAsyncKeyState('A')) pos.x -= speed;
        if (GetAsyncKeyState('D')) pos.x += speed;
        if (GetAsyncKeyState(VK_SPACE)) pos.z += speed;
        if (GetAsyncKeyState(VK_LCONTROL)) pos.z -= speed;

        if (localPlayer) {
            localPlayer->Teleport(pos);
        }
    }

    void ApplySlapper() {
        if (!slapperEnabled) return;

        auto pNetGame = samp::RefNetGame();
        if (!pNetGame || !pNetGame->m_pPools || !pNetGame->m_pPools->m_pPlayer) return;

        auto pPlayerPool = pNetGame->m_pPools->m_pPlayer;
        auto pLocal = pPlayerPool->GetLocalPlayer();
        if (!pLocal || !pLocal->m_pPed) return;

        static DWORD phaseTimer = GetTickCount();
        static bool modeStrike = true;
        DWORD now = GetTickCount();

        if (modeStrike) {
            int targetId = -1;
            float minDistance = slapperRadius;

            for (int i = 0; i <= pPlayerPool->m_nLargestId; i++) {
                if (i == pPlayerPool->m_localInfo.m_nId) continue;
                auto pRemotePlayer = pPlayerPool->GetPlayer(i);
                if (!pRemotePlayer || !pRemotePlayer->m_pPed) continue;

                float dist = pLocal->m_pPed->GetDistanceToEntity(pRemotePlayer->m_pPed);
                if (dist < minDistance) {
                    minDistance = dist;
                    targetId = i;
                }
            }

            if (targetId != -1) {
                auto pTarget = pPlayerPool->GetPlayer(targetId);
                sampapi::CVector targetPos;
                pTarget->m_pPed->GetBonePosition(1, &targetPos);

                for (int i = 0; i < 15; i++) {
                    sampapi::CVector syncPos = targetPos;
                    syncPos.x += (float)((rand() % 100 - 50) / 100.0f);
                    syncPos.y += (float)((rand() % 100 - 50) / 100.0f);
                    syncPos.z += (float)(i * 0.05f); 

                    sampapi::CVector moveSpeed;
                    moveSpeed.x = (rand() % 2 == 0 ? slapperForce : -slapperForce);
                    moveSpeed.y = (rand() % 2 == 0 ? slapperForce : -slapperForce);
                    moveSpeed.z = 100.0f;

                    if (pLocal->m_nCurrentVehicle == 0xFFFF) {
                        pLocal->m_onfootData.m_position = syncPos;
                        pLocal->m_onfootData.m_speed = moveSpeed;
                        pLocal->m_lastUpdate = 0; 
                        pLocal->SendOnfootData();
                    }
                    else {
                        pLocal->m_incarData.m_position = syncPos;
                        pLocal->m_incarData.m_speed = moveSpeed;
                        pLocal->m_lastUpdate = 0;
                        pLocal->SendIncarData();
                    }
                }
            }

            if (now - phaseTimer > 100) {
                modeStrike = false;
                phaseTimer = now;
            }
        }
        else {
            pLocal->m_lastUpdate = now + slapperLagInterval;
            pLocal->m_lastAnyUpdate = now + slapperLagInterval;

            if (now - phaseTimer > (DWORD)slapperLagInterval) {
                modeStrike = true;
                phaseTimer = now;
            }
        }
    }

    void GodMode() {
        static bool lastState = false;
        samp::CPed* localPlayer = GetLocalPlayer();
        if (!localPlayer) return;

        if (godModeEnabled) {
            localPlayer->SetImmunities(TRUE, TRUE, TRUE, TRUE, TRUE);
            if (localPlayer->GetHealth() < 100.0f) {
                localPlayer->SetHealth(100.0f);
            }
            lastState = true;
        }
        else if (lastState) {
            localPlayer->SetImmunities(FALSE, FALSE, FALSE, FALSE, FALSE);
            lastState = false;
        }
    }

    void SpeedHack() {
        if (!speedHackEnabled) return;

        samp::CPed* localPlayer = GetLocalPlayer();
        if (!localPlayer || !localPlayer->m_pGameEntity) return;

        if (GetAsyncKeyState(VK_MENU) & 0x8000) {
            sampapi::CVector currentSpeed;
            localPlayer->GetSpeed(&currentSpeed);
            currentSpeed.x *= speedMultiplier;
            currentSpeed.y *= speedMultiplier;
            localPlayer->SetSpeed(currentSpeed);
        }
    }

    // ============================================
    // TRIGGERBOT
    // ============================================
    void TriggerBot(samp::CPed* localPlayer, std::vector<samp::CPed*>& players,
        int screenWidth, int screenHeight) {
        if (!triggerBotEnabled || !localPlayer) return;

        static DWORD lastShotTime = 0;
        DWORD currentTime = GetTickCount();

        if (currentTime - lastShotTime < triggerDelay) return;

        float crosshairX = (screenWidth / 2.0f) + aimOffsetX;
        float crosshairY = (screenHeight / 2.0f) + aimOffsetY;

        for (auto& player : players) {
            if (!player || player->IsDead()) continue;

            sampapi::CVector worldPos;
            player->GetBonePosition(3, &worldPos);

            ImVec2 screenPos;
            if (WorldToScreen(worldPos, screenPos)) {
                float dist = sqrtf(powf(screenPos.x - crosshairX, 2) +
                    powf(screenPos.y - crosshairY, 2));

                if (dist < 20.0f) {
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                    Sleep(50);
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    lastShotTime = currentTime;
                    stats.totalShots++;
                    break;
                }
            }
        }
    }

    // ============================================
    // RADAR
    // ============================================
    void DrawRadar(samp::CPed* localPlayer, std::vector<samp::CPed*>& players) {
        if (!radarEnabled || !localPlayer) return;

        ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(10, 100), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Radar", &radarEnabled, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
            ImGui::End();
            return;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        ImVec2 radarCenter = ImVec2(winPos.x + winSize.x / 2, winPos.y + winSize.y / 2);

        drawList->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
            IM_COL32(30, 30, 30, 200));
        drawList->AddCircle(radarCenter, 90.0f, IM_COL32(100, 100, 100, 255), 32);
        drawList->AddCircleFilled(radarCenter, 4.0f, IM_COL32(0, 255, 0, 255));

        sampapi::CMatrix localMat;
        localPlayer->GetMatrix(&localMat);
        sampapi::CVector localPos = localMat.pos;
        float playerAngle = atan2(localMat.at.y, localMat.at.x);

        for (auto& player : players) {
            if (!player || player == localPlayer || player->IsDead()) continue;

            sampapi::CMatrix targetMat;
            player->GetMatrix(&targetMat);
            sampapi::CVector targetPos = targetMat.pos;

            float dx = targetPos.x - localPos.x;
            float dy = targetPos.y - localPos.y;
            float dist = sqrt(dx * dx + dy * dy);

            if (dist > radarZoom) continue;

            float relX = dx * cos(playerAngle) + dy * sin(playerAngle);
            float relY = dy * cos(playerAngle) - dx * sin(playerAngle);

            float posX = radarCenter.x + (relY / radarZoom) * 90.0f;
            float posY = radarCenter.y - (relX / radarZoom) * 90.0f;

            drawList->AddCircleFilled(ImVec2(posX, posY), 3.0f, IM_COL32(255, 0, 0, 255));
        }

        ImGui::End();
    }

    // ============================================
    // CUSTOM CROSSHAIR
    // ============================================
    void DrawCustomCrosshair(int screenWidth, int screenHeight) {
        if (!customCrosshairEnabled) return;

        float centerX = (screenWidth / 2.0f) + aimOffsetX;
        float centerY = (screenHeight / 2.0f) + aimOffsetY;

        auto drawList = ImGui::GetBackgroundDrawList();
        ImU32 color = ImGui::ColorConvertFloat4ToU32(crosshairColor);

        switch (crosshairStyle) {
        case 0: 
            drawList->AddLine(ImVec2(centerX - crosshairSize, centerY),
                ImVec2(centerX + crosshairSize, centerY), color, 2.0f);
            drawList->AddLine(ImVec2(centerX, centerY - crosshairSize),
                ImVec2(centerX, centerY + crosshairSize), color, 2.0f);
            break;
        case 1: 
            drawList->AddCircleFilled(ImVec2(centerX, centerY), crosshairSize / 3, color);
            break;
        case 2: 
            drawList->AddCircle(ImVec2(centerX, centerY), crosshairSize, color, 32, 2.0f);
            break;
        case 3: 
            drawList->AddLine(ImVec2(centerX - crosshairSize, centerY - crosshairSize),
                ImVec2(centerX + crosshairSize, centerY - crosshairSize), color, 2.0f);
            drawList->AddLine(ImVec2(centerX, centerY - crosshairSize),
                ImVec2(centerX, centerY + crosshairSize), color, 2.0f);
            break;
        }
    }

    // ============================================
    // IMGUI STYLES 
    // ============================================

    void SetupImGuiStyle() {
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 8.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.PopupRounding = 6.0f;
        style.ScrollbarRounding = 4.0f;
        style.FramePadding = ImVec2(5, 5);
        style.ItemSpacing = ImVec2(8, 10);

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.50f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
        colors[ImGuiCol_Border] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.30f, 0.70f, 1.00f, 1.00f);
        colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
        colors[ImGuiCol_Header] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
        colors[ImGuiCol_Tab] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.60f, 1.00f, 0.80f);
        colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.60f, 1.00f, 1.00f);
    }

    // ============================================
    // MAIN ENDSCENE HOOK
    // ============================================
    HRESULT STDMETHODCALLTYPE Hooked_EndScene(IDirect3DDevice9* pDevice) {
        if (pDevice->TestCooperativeLevel() != D3D_OK)
            return Original_EndScene(pDevice);

        static void* dwAllowedReturn = nullptr;
        if (dwAllowedReturn == nullptr) dwAllowedReturn = _ReturnAddress();
        if (dwAllowedReturn != _ReturnAddress()) return Original_EndScene(pDevice);

        if (!init) {
            D3DDEVICE_CREATION_PARAMETERS cp;
            if (SUCCEEDED(pDevice->GetCreationParameters(&cp))) {
                game_hwnd = cp.hFocusWindow;
                if (game_hwnd) {
                    ImGui::CreateContext();
                    ImGui_ImplWin32_Init(game_hwnd);
                    ImGui_ImplDX9_Init(pDevice);
                    ImGui::GetIO().IniFilename = nullptr;
                    Original_WndProc = (WNDPROC)SetWindowLongPtr(game_hwnd, GWLP_WNDPROC, (LONG_PTR)Hooked_WndProc);
                    stats.Start();
                    init = true;
                }
            }
        }

        if (GetAsyncKeyState(VK_INSERT) & 1) {
            show_menu = !show_menu;
            if (show_menu) {
                ClipCursor(NULL);
                while (ShowCursor(TRUE) < 0);
            }
            else {
                while (ShowCursor(FALSE) >= 0);
            }
        }

        if (GetAsyncKeyState(VK_END) & 1) {
            if (panicMode.IsActive()) {
                panicMode.Deactivate();
            }
            else {
                panicMode.Activate();
            }
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        auto pNetGame = samp::RefNetGame();
        ImGuiIO& io = ImGui::GetIO();
        int sw = (int)io.DisplaySize.x;
        int sh = (int)io.DisplaySize.y;

        if (pNetGame && pNetGame->m_pPools) {
            samp::CPed* localPlayer = GetLocalPlayer();

            std::vector<samp::CPed*> players;
            auto pPlayerPool = pNetGame->m_pPools->m_pPlayer;
            if (pPlayerPool) {
                for (int i = 0; i < 1004; i++) {
                    auto pRemote = pPlayerPool->GetPlayer(i);
                    if (pRemote && pRemote->m_pPed) {
                        players.push_back((samp::CPed*)pRemote->m_pPed);

                        const char* name = pPlayerPool->GetName(i);
                        if (name) adminDetector.AnalyzePlayer(name, (samp::CPed*)pRemote->m_pPed);
                    }
                }
            }

            if (localPlayer) {
                if (aimbotEnabled && (!aimbotKeyEnabled || (GetAsyncKeyState(VK_RBUTTON) & 0x8000))) {
                    float crosshairX = (sw / 2.0f) + aimOffsetX;
                    float crosshairY = (sh / 2.0f) + aimOffsetY;

                    ImGui::GetBackgroundDrawList()->AddCircle(
                        ImVec2(crosshairX, crosshairY),
                        aimbotFovRadius,
                        IM_COL32(255, 255, 255, 100),
                        64, 1.0f
                    );

                    samp::CPed* target = smartAimbot.FindBestTarget(localPlayer, players, crosshairX, crosshairY);
                    if (target) {
                        smartAimbot.AimAt(target, crosshairX, crosshairY);
                    }
                }

                ApplyNoRecoil();
                ApplyNoSpread();
                ApplyRapidFire();
                ApplyNoReload();

                GodMode();
                SpeedHack();
                AirBreak(localPlayer);
                ApplySlapper();

                TriggerBot(localPlayer, players, sw, sh);

                if (espEnabled) {
                    for (auto player : players) {
                        DrawPlayerESP(localPlayer, player, sw, sh);
                    }
                }
                if (vehicleEspEnabled) DrawVehicleESP(sw, sh);
                if (radarEnabled) DrawRadar(localPlayer, players);
                DrawCustomCrosshair(sw, sh);
            }
        }

        panicMode.DrawIndicator();
        adminDetector.DrawWarnings();
        stats.DrawStats();
        adminDetector.CleanupOldEntries();



        if (show_menu) {
            SetupImGuiStyle(); 
            ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("SAMP INTERNAL CHEAT", &show_menu, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {

                if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_None)) {

                    // --- AIMBOT TAB ---
                    if (ImGui::BeginTabItem("Aimbot")) {
                        ImGui::BeginChild("AimGeneral", ImVec2(0, 0), true);
                        ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Main Settings");
                        ImGui::Checkbox("Enable Aimbot", &aimbotEnabled);
                        ImGui::SameLine(); ImGui::Checkbox("Require RMB", &aimbotKeyEnabled);

                        ImGui::PushItemWidth(-140);
                        ImGui::DragFloat("FOV Radius", &aimbotFovRadius, 1.0f, 10.0f, 500.0f, "%.1f");
                        ImGui::DragFloat("Range", &aimbotRange, 1.0f, 10.0f, 200.0f, "%.1f");
                        ImGui::DragFloat("Smoothing", &aimbotSmooth, 0.1f, 1.0f, 10.0f, "%.1f");

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Prediction & Safety");
                        ImGui::Checkbox("Enable Prediction", &aimbotPrediction);
                        if (aimbotPrediction) ImGui::SliderFloat("Pred. Time", &aimbotPredictionTime, 0.05f, 0.5f);

                        ImGui::SliderFloat("Reaction (ms)", &aimbotSafety.reactionTime, 0.0f, 500.0f, "%.0f");
                        ImGui::Checkbox("Human Error", &aimbotSafety.addHumanError);

                        ImGui::Separator();
                        ImGui::TextDisabled("Calibration:");
                        ImGui::DragFloat("Offset X", &aimOffsetX, 0.5f, -150.f, 150.f);
                        ImGui::DragFloat("Offset Y", &aimOffsetY, 0.5f, -150.f, 150.f);
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    // --- WEAPON TAB ---
                    if (ImGui::BeginTabItem("Weapon")) {
                        ImGui::BeginChild("WeaponSettings", ImVec2(0, 0), true);
                        ImGui::Columns(2, "weapon_cols", false);
                        ImGui::Checkbox("No Recoil", &noRecoilEnabled);
                        ImGui::Checkbox("No Spread", &noSpreadEnabled);
                        ImGui::NextColumn();
                        ImGui::Checkbox("No Reload", &noReloadEnabled);
                        ImGui::Checkbox("Rapid Fire", &rapidFireEnabled);
                        ImGui::Columns(1);

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "TriggerBot");
                        ImGui::Checkbox("Enable Trigger", &triggerBotEnabled);
                        if (triggerBotEnabled) ImGui::SliderFloat("Delay (ms)", &triggerDelay, 0.0f, 200.0f);
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    // --- ESP TAB ---
                    if (ImGui::BeginTabItem("Visuals")) {
                        ImGui::BeginChild("ESPSettings", ImVec2(0, 0), true);
                        ImGui::Checkbox("Enable Master ESP", &espEnabled);
                        ImGui::Separator();

                        ImGui::Columns(2, "esp_cols", false);
                        ImGui::Checkbox("Names", &espNames);
                        ImGui::Checkbox("Health/Armor", &espHealthArmor);
                        ImGui::Checkbox("Box 2D", &espBox);
                        ImGui::Checkbox("Skeleton", &espSkeleton);
                        ImGui::NextColumn();
                        ImGui::Checkbox("Lines", &espLines);
                        ImGui::Checkbox("Distance", &espDistance);
                        ImGui::Checkbox("Weapon", &espWeapon);
                        ImGui::Checkbox("Vehicle ESP", &vehicleEspEnabled);
                        ImGui::Columns(1);

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0.2f, 0.6f, 1.0f, 1.0f), "Filters");
                        ImGui::Checkbox("Hide Admins", &espSettings.hideAdmins);
                        ImGui::Checkbox("Interior Check", &espSettings.hideInInterior);
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    // --- MOVEMENT TAB ---
                    if (ImGui::BeginTabItem("Movement")) {
                        ImGui::BeginChild("MoveSettings", ImVec2(0, 0), true);
                        ImGui::Checkbox("God Mode", &godModeEnabled);
                        ImGui::Checkbox("Air Break", &airBreakEnabled);

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Exploits");
                        ImGui::Checkbox("Speed Hack (ALT)", &speedHackEnabled);
                        if (speedHackEnabled) ImGui::SliderFloat("Multiplier", &speedMultiplier, 1.0f, 5.0f);

                        ImGui::Checkbox("SLAPPER", &slapperEnabled);
                        if (slapperEnabled) {
                            ImGui::Indent();
                            ImGui::DragFloat("Slap Force", &slapperForce, 1.0f, 10.0f, 1000.0f);
                            ImGui::DragFloat("Magnet Radius", &slapperRadius, 0.5f, 5.0f, 100.0f);
                            ImGui::DragInt("Lag Phase (ms)", &slapperLagInterval, 5, 50, 1000);
                            ImGui::Unindent();
                        }
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    // --- MISC TAB ---
                    if (ImGui::BeginTabItem("Misc")) {
                        ImGui::BeginChild("MiscSettings", ImVec2(0, 0), true);
                        ImGui::Checkbox("Show Stats Overlay", &stats.showStats);
                        ImGui::Checkbox("Admin Warnings", &adminDetector.showWarnings);

                        ImGui::Spacing();
                        if (ImGui::Button("Reset All Session Data", ImVec2(-1, 30))) {
                            stats = Statistics();
                            stats.Start();
                        }

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Panic Key: [END]");
                        ImGui::TextWrapped("Pressing END will immediately disable all active features and close the menu.");
                        ImGui::EndChild();
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
            }
            ImGui::End();
        }
        else {
            io.MouseDrawCursor = false;
        }

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

        return Original_EndScene(pDevice);
    }

    // ============================================
    // DLL ENTRY POINT
    // ============================================
    void* GetVTableFunction(int index) {
        IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
        if (!pD3D) return nullptr;

        D3DPRESENT_PARAMETERS d3dpp = { 0 };
        d3dpp.Windowed = TRUE;
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow = GetDesktopWindow();

        IDirect3DDevice9* pDummyDevice = nullptr;
        if (pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice) == S_OK) {
            void** vTable = *(void***)pDummyDevice;
            void* func = vTable[index];
            pDummyDevice->Release();
            pD3D->Release();
            return func;
        }
        pD3D->Release();
        return nullptr;
    }

    DWORD WINAPI MainThread(LPVOID lpReserved) {
        while (!GetModuleHandleA("d3d9.dll") || !GetModuleHandleA("samp.dll"))
            Sleep(500);

        MH_Initialize();
        MH_CreateHook(GetVTableFunction(42), &Hooked_EndScene, (LPVOID*)&Original_EndScene);
        MH_CreateHook(GetVTableFunction(16), &Hooked_Reset, (LPVOID*)&Original_Reset);
        MH_CreateHookApi(L"user32.dll", "SetCursorPos", &Hooked_SetCursorPos, (LPVOID*)&Original_SetCursorPos);
        MH_EnableHook(MH_ALL_HOOKS);

        return TRUE;
    }

    BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
        if (reason == DLL_PROCESS_ATTACH) {
            DisableThreadLibraryCalls(hModule);
            CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, nullptr);
        }
        return TRUE;
    }