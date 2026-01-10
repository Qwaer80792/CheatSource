#define IMGUI_DEFINE_MATH_OPERATORS
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NO_STRICT

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <winsock2.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <intrin.h>

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

#include <sampapi/sampapi.h>
#include <sampapi/CVector.h>
#include <sampapi/CMatrix.h>
#include <sampapi/0.3.7-R3-1/CNetGame.h>
#include <sampapi/0.3.7-R3-1/CPlayerPool.h>
#include <sampapi/0.3.7-R3-1/CRemotePlayer.h>
#include <sampapi/0.3.7-R3-1/CPlayerInfo.h>
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
bool show_menu = true;
HWND game_hwnd = nullptr;
sampapi::CMatrix localMat;

bool espEnabled = true;
bool espNames = true;
bool espHealthArmor = true;
bool aimbotEnabled = false;
bool aimbotKeyEnabled = true;
float aimbotRange = 100.0f;
float aimbotFovRadius = 150.0f;
bool airBreakEnabled = false;
bool firstActivation = true;
bool rapidFireEnabled = false;
bool noRecoilEnabled = false;
bool noSpreadEnabled = false;
bool godModeEnabled = false;
bool radarEnabled = false;
float radarZoom = 100.0f;
bool speedHackEnabled = false;
float speedMultiplier = 2.0f;
bool vehicleEspEnabled = false;
bool autoShootEnabled = false;
bool autoShootKeyEnabled = true;

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

void RefreshPlayers(std::vector<samp::CPed*>& playersList)
{
    playersList.clear();
    auto pNetGame = samp::RefNetGame();
    if (!pNetGame || !pNetGame->m_pPools || !pNetGame->m_pPools->m_pPlayer) return;

    auto pPlayerPool = pNetGame->m_pPools->m_pPlayer;
    for (int i = 0; i < 1004; i++)
    {
        auto pRemotePlayer = pPlayerPool->GetPlayer(i);
        if (pRemotePlayer && pRemotePlayer->m_pPed)
        {
            playersList.push_back((samp::CPed*)pRemotePlayer->m_pPed);
        }
    }
}

std::vector<samp::CVehicle*> GetVehicles()
{
    std::vector<samp::CVehicle*> vehicles;

    auto netGame = samp::RefNetGame();
    if (!netGame || !netGame->m_pPools) return vehicles;

    auto vehiclePool = netGame->m_pPools->m_pVehicle;
    if (!vehiclePool) return vehicles;

    for (int i = 0; i < samp::CVehiclePool::MAX_VEHICLES; i++)
    {
        if (vehiclePool->m_pObject[i])
        {
            auto pVehicle = vehiclePool->m_pObject[i];
            if (pVehicle)
            {
                vehicles.push_back(pVehicle);
            }
        }
    }
    return vehicles;
}

bool WorldToScreen(const sampapi::CVector& worldPos, ImVec2& screenPos)
{
    float fX, fY, fW, fH;
    typedef bool(__cdecl* CalcScreenCoors_t)(const sampapi::CVector&, sampapi::CVector*, float*, float*, bool, bool);
    static CalcScreenCoors_t CalcScreenCoors = (CalcScreenCoors_t)0x70CE30;

    sampapi::CVector out;
    if (CalcScreenCoors(worldPos, &out, &fW, &fH, true, true))
    {
        screenPos.x = out.x;
        screenPos.y = out.y;
        return true;
    }
    return false;
}

void DrawESPLine(const sampapi::CVector& worldPos, ImU32 color, const char* name)
{
    ImVec2 screen;
    if (WorldToScreen(worldPos, screen)) {
        auto drawList = ImGui::GetBackgroundDrawList();
        float sw = ImGui::GetIO().DisplaySize.x;
        float sh = ImGui::GetIO().DisplaySize.y;

        drawList->AddLine(ImVec2(sw / 2, sh), screen, color);
        if (name) drawList->AddText(screen, IM_COL32(255, 255, 255, 255), name);
    }
}

void DrawVehicleESP(samp::CVehicle* vehicle, int screenWidth, int screenHeight)
{
    if (!vehicle || !vehicle->m_pGameEntity) return;

    sampapi::CMatrix vehMatrix;
    vehicle->GetMatrix(&vehMatrix);
    sampapi::CVector worldPos = vehMatrix.pos;

    ImVec2 screenPos;
    if (WorldToScreen(worldPos, screenPos))
    {
        int vehicleId = -1;
        auto pNetGame = samp::RefNetGame();
        if (pNetGame && pNetGame->m_pPools && pNetGame->m_pPools->m_pVehicle) {
            auto pVehiclePool = pNetGame->m_pPools->m_pVehicle;
            for (int i = 0; i < 2000; i++) {
                if (pVehiclePool->m_pObject[i] && pVehiclePool->m_pObject[i]->m_pGameVehicle == vehicle->m_pGameVehicle) {
                    vehicleId = i;
                    break;
                }
            }
        }

        auto drawList = ImGui::GetBackgroundDrawList();

        char buf[32];
        sprintf(buf, "Veh ID: %d", vehicleId);
        drawList->AddText(screenPos, IM_COL32(0, 255, 255, 255), buf);

        drawList->AddLine(ImVec2(screenWidth / 2.0f, (float)screenHeight), screenPos, IM_COL32(0, 255, 255, 150));
    }
}

void DrawPlayerESP(sampapi::v037r3::CPed* player, int screenWidth, int screenHeight)
{
    if (!player || !player->m_pGamePed) return;

    sampapi::CVector headPos;
    player->GetBonePosition(8, &headPos);

    headPos.z += 0.2f;

    ImVec2 screenPos;
    if (WorldToScreen(headPos, screenPos))
    {
        auto drawList = ImGui::GetBackgroundDrawList();

        const char* szName = "Player";
        auto pNetGame = sampapi::v037r3::RefNetGame();
        if (pNetGame && pNetGame->m_pPools && pNetGame->m_pPools->m_pPlayer)
        {
            sampapi::ID nId = pNetGame->m_pPools->m_pPlayer->Find(player->m_pGamePed);
            if (nId != -1) szName = pNetGame->m_pPools->m_pPlayer->GetName(nId);
        }

        float health = *(float*)((DWORD)player->m_pGamePed + 0x540);

        drawList->AddLine(
            ImVec2((float)screenWidth / 2.0f, (float)screenHeight),
            screenPos,
            IM_COL32(255, 255, 0, 120)
        );

        ImVec2 textSize = ImGui::CalcTextSize(szName);
        ImVec2 textPos = ImVec2(screenPos.x - (textSize.x / 2.0f), screenPos.y - 15.0f);
        drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), szName);

        float barWidth = 35.0f;
        float healthRatio = (health > 100.0f ? 100.0f : (health < 0.0f ? 0.0f : health)) / 100.0f;

        ImVec2 barPos = ImVec2(screenPos.x - barWidth / 2.0f, screenPos.y - 2.0f);

        drawList->AddRectFilled(barPos, ImVec2(barPos.x + barWidth, barPos.y + 4), IM_COL32(0, 0, 0, 200));

        drawList->AddRectFilled(barPos, ImVec2(barPos.x + (barWidth * healthRatio), barPos.y + 4), IM_COL32(0, 255, 0, 255));
    }
}

void GetAngleToTarget(sampapi::CVector target, sampapi::CVector owner, float& yaw, float& pitch)
{
    sampapi::CVector diff = { target.x - owner.x, target.y - owner.y, target.z - owner.z };
    float distance = sqrt(diff.x * diff.x + diff.y * diff.y);

    yaw = atan2(diff.y, diff.x) - (3.14159265f / 2.0f);
    pitch = atan2(diff.z, distance);
}

void Aimbot(samp::CPed* localPlayer, std::vector<samp::CPed*>& players)
{
    if (!aimbotEnabled || !localPlayer) return;
    if (aimbotKeyEnabled && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) return;

    float closestFov = aimbotFovRadius;
    samp::CPed* bestTarget = nullptr;
    sampapi::CVector targetHeadPos;

    for (auto player : players)
    {
        if (!player || player->IsDead()) continue;

        sampapi::CVector headPos;
        player->GetBonePosition(8, &headPos); 

        ImVec2 screenPos;
        if (WorldToScreen(headPos, screenPos))
        {
            float centerX = ImGui::GetIO().DisplaySize.x / 2.0f;
            float centerY = ImGui::GetIO().DisplaySize.y / 2.0f;

            float fovDist = sqrtf(powf(screenPos.x - centerX, 2) + powf(screenPos.y - centerY, 2));

            if (fovDist < closestFov)
            {
                closestFov = fovDist;
                bestTarget = player;
                targetHeadPos = headPos;
            }
        }
    }

    if (bestTarget)
    {
        auto& aimData = samp::AimStuff::RefLocalPlayerAim();

        sampapi::CVector direction;
        direction.x = targetHeadPos.x - aimData.source.x;
        direction.y = targetHeadPos.y - aimData.source.y;
        direction.z = targetHeadPos.z - aimData.source.z;

        float length = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (length > 0.0f)
        {
            direction.x /= length;
            direction.y /= length;
            direction.z /= length;
        }

        float smooth = 15.0f;

        aimData.front.x += (direction.x - aimData.front.x) / smooth;
        aimData.front.y += (direction.y - aimData.front.y) / smooth;
        aimData.front.z += (direction.z - aimData.front.z) / smooth;

        samp::AimStuff::UpdateAim();
    }
}

void ApplyNoSpreadLib()
{
    if (!noSpreadEnabled) return;

    samp::CGame* pGame = samp::RefGame();
    if (!pGame || !pGame->m_pPlayerPed) return;

    int weaponId = pGame->m_pPlayerPed->GetCurrentWeapon();
    if (weaponId < 22) return;

    auto pInfo = pGame->GetWeaponInfo(weaponId, 2);
    if (pInfo)
    {
        float* pAccuracy = (float*)((uintptr_t)pInfo + 0x20);
        float* pMoveSpeedAccuracy = (float*)((uintptr_t)pInfo + 0x24);

        *pAccuracy = 100.0f;
        *pMoveSpeedAccuracy = 100.0f;
    }
}

void ProcessWeaponHacks()
{
    auto pGame = samp::RefGame();
    if (!pGame || !pGame->m_pPlayerPed) return;

    if (noRecoilEnabled || noSpreadEnabled)
    {
        samp::AimStuff::UpdateAim();
        samp::AimStuff::ApplyAim();
    }

    int weaponId = pGame->m_pPlayerPed->GetCurrentWeapon();
    if (weaponId >= 22)
    {
        auto pInfo = pGame->GetWeaponInfo(weaponId, 2);
        if (pInfo)
        {
            if (noSpreadEnabled)
            {
                float* pAccuracy = (float*)((uintptr_t)pInfo + 0x20);
                *pAccuracy = 100.0f;
            }
        }
    }
}

void NoSpread()
{
    if (!noSpreadEnabled) return;
    sampapi::v037r3::AimStuff::UpdateAim();
    sampapi::v037r3::AimStuff::ApplyAim();
}

void AirBreak(samp::CPed* localPlayer)
{
    if (!airBreakEnabled)
    {
        firstActivation = true;
        return;
    }

    static sampapi::CVector pos;

    if (firstActivation)
    {
        if (localPlayer)
        {
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

    if (localPlayer)
    {
        localPlayer->Teleport(pos);
    }
}

void GodMode()
{
    static bool lastState = false;
    samp::CPed* localPlayer = GetLocalPlayer();
    if (!localPlayer) return;

    if (godModeEnabled)
    {
        localPlayer->SetImmunities(TRUE, TRUE, TRUE, TRUE, TRUE);

        if (localPlayer->GetHealth() < 100.0f)
        {
            localPlayer->SetHealth(100.0f);
        }
        lastState = true;
    }
    else if (lastState)
    {
        localPlayer->SetImmunities(FALSE, FALSE, FALSE, FALSE, FALSE);
        lastState = false;
    }
}

void SpeedHack()
{
    if (!speedHackEnabled) return;

    samp::CPed* localPlayer = GetLocalPlayer();
    if (!localPlayer || !localPlayer->m_pGameEntity) return;

    if (GetAsyncKeyState(VK_MENU) & 0x8000)
    {
        sampapi::CVector currentSpeed;

        localPlayer->GetSpeed(&currentSpeed);

        currentSpeed.x *= speedMultiplier;
        currentSpeed.y *= speedMultiplier;

        localPlayer->SetSpeed(currentSpeed);
    }
}

void AutoShoot(samp::CPed* localPlayer, std::vector<samp::CPed*>& players, int screenWidth, int screenHeight)
{
    if (!autoShootEnabled || !localPlayer) return;

    if (autoShootKeyEnabled && !(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) return;

    static DWORD lastShotTime = 0;
    DWORD currentTime = GetTickCount();

    for (auto& player : players)
    {
        if (!player || player->IsDead()) continue;

        sampapi::CVector worldPos;
        player->GetBonePosition(3, &worldPos);

        ImVec2 screenPos;
        if (WorldToScreen(worldPos, screenPos))
        {
            float centerX = screenWidth / 2.0f;
            float centerY = screenHeight / 2.0f;

            float distanceToCenter = sqrtf(powf(screenPos.x - centerX, 2) + powf(screenPos.y - centerY, 2));

            if (distanceToCenter < 15.0f)
            {
                if (currentTime - lastShotTime > 100) 
                {
                    INPUT input[2] = { 0 };

                    input[0].type = INPUT_MOUSE;
                    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

                    input[1].type = INPUT_MOUSE;
                    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;

                    SendInput(2, input, sizeof(INPUT));

                    lastShotTime = currentTime;
                    break; 
                }
            }
        }
    }
}

void RapidFire()
{
    if (!rapidFireEnabled) return;

    auto pGame = samp::RefGame();
    if (!pGame || !pGame->m_pPlayerPed) return;

    int weaponId = pGame->m_pPlayerPed->GetCurrentWeapon();
    auto pInfo = pGame->GetWeaponInfo(weaponId, 2);

    if (pInfo)
    {
        *(float*)((uintptr_t)pInfo + 0x60) = 0.1f;
    }
}

void ApplyNoRecoil()
{
    if (!noRecoilEnabled) return;

    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
    {
        *(float*)0xB6EC14 = 0.0f;
        *(float*)0xB6EC18 = 0.0f;
    }
}

void TeleportToPlayer(samp::CPed* target)
{
    if (!target || !target->m_pGameEntity) return;

    samp::CGame* pGame = samp::RefGame();
    if (!pGame || !pGame->m_pPlayerPed) return;

    sampapi::CMatrix targetMatrix;
    target->GetMatrix(&targetMatrix);

    sampapi::CVector targetPos = targetMatrix.pos;

    targetPos.z += 1.0f;
    pGame->m_pPlayerPed->Teleport(targetPos);
}

void DrawRadar(samp::CPed* localPlayer, std::vector<samp::CPed*>& players, int screenWidth, int screenHeight)
{
    if (!radarEnabled || !localPlayer) return;

    ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Radar", &radarEnabled, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
    {
        ImGui::End();
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec2 radarCenter = ImVec2(winPos.x + winSize.x / 2, winPos.y + winSize.y / 2);

    drawList->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y), IM_COL32(30, 30, 30, 150));
    drawList->AddCircleFilled(radarCenter, 4.0f, IM_COL32(0, 255, 0, 255));

    sampapi::CMatrix localMat;
    localPlayer->GetMatrix(&localMat);
    sampapi::CVector localPos = localMat.pos;

    float playerAngle = atan2(localMat.at.y, localMat.at.x);

    for (auto& player : players)
    {
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

HRESULT STDMETHODCALLTYPE Hooked_EndScene(IDirect3DDevice9* pDevice)
{
    if (pDevice->TestCooperativeLevel() != D3D_OK)
        return Original_EndScene(pDevice);

    static void* dwAllowedReturn = nullptr;
    if (dwAllowedReturn == nullptr) dwAllowedReturn = _ReturnAddress();
    if (dwAllowedReturn != _ReturnAddress()) return Original_EndScene(pDevice);

    if (!init)
    {
        D3DDEVICE_CREATION_PARAMETERS cp;
        if (SUCCEEDED(pDevice->GetCreationParameters(&cp)))
        {
            game_hwnd = cp.hFocusWindow;
            if (game_hwnd)
            {
                ImGui::CreateContext();
                ImGui_ImplWin32_Init(game_hwnd);
                ImGui_ImplDX9_Init(pDevice);
                ImGui::GetIO().IniFilename = nullptr;
                Original_WndProc = (WNDPROC)SetWindowLongPtr(game_hwnd, GWLP_WNDPROC, (LONG_PTR)Hooked_WndProc);
                init = true;
            }
        }
    }

    if (GetAsyncKeyState(VK_INSERT) & 1)
    {
        show_menu = !show_menu;
        if (show_menu)
        {
            ClipCursor(NULL);
            while (ShowCursor(TRUE) < 0);
        }
        else
        {
            while (ShowCursor(FALSE) >= 0);
        }
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    auto pNetGame = samp::RefNetGame();
    auto pGame = samp::RefGame();
    ImGuiIO& io = ImGui::GetIO();
    int screenWidth = (int)io.DisplaySize.x;
    int screenHeight = (int)io.DisplaySize.y;

    if (pNetGame && pNetGame->m_pPools)
    {
        samp::CPed* localPlayer = (pGame && pGame->m_pPlayerPed) ? (samp::CPed*)pGame->m_pPlayerPed : nullptr;

        std::vector<samp::CPed*> players;
        std::vector<samp::CVehicle*> vehicles;

        auto pPlayerPool = pNetGame->m_pPools->m_pPlayer;
        if (pPlayerPool)
        {
            for (int i = 0; i < 1004; i++)
            {
                auto pRemotePlayer = pPlayerPool->GetPlayer(i);
                if (pRemotePlayer && pRemotePlayer->m_pPed)
                    players.push_back((samp::CPed*)pRemotePlayer->m_pPed);
            }
        }

        auto pVehiclePool = pNetGame->m_pPools->m_pVehicle;
        if (pVehiclePool)
        {
            for (int i = 0; i < 2000; i++)
            {
                auto pSAMPVehicle = pVehiclePool->m_pObject[i];
                if (pSAMPVehicle && pSAMPVehicle->m_pGameVehicle)
                    vehicles.push_back((samp::CVehicle*)pSAMPVehicle->m_pGameVehicle);
            }
        }

        if (localPlayer)
        {
            Aimbot(localPlayer, players);
            ApplyNoRecoil();
            NoSpread();
            GodMode();
            SpeedHack();
            RapidFire();
            AutoShoot(localPlayer, players, screenWidth, screenHeight);
            AirBreak(localPlayer);
        }

        if (espEnabled)
            for (auto player : players) DrawPlayerESP(player, screenWidth, screenHeight);

        if (vehicleEspEnabled)
            for (auto vehicle : vehicles) DrawVehicleESP(vehicle, screenWidth, screenHeight);

        if (localPlayer && radarEnabled)
            DrawRadar(localPlayer, players, screenWidth, screenHeight);

        if (show_menu)
        {
            io.MouseDrawCursor = true;
            ImGui::SetNextWindowSize(ImVec2(550, 420), ImGuiCond_FirstUseEver);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

            ImGui::Begin("Radmir Internal Final v3.5", &show_menu, ImGuiWindowFlags_NoCollapse);

            if (ImGui::CollapsingHeader("Debug Matrix"))
            {
                float* m = (float*)0xB6FA28;
                ImGui::Text("Address: 0xB6FA28");
                for (int i = 0; i < 4; i++) {
                    ImGui::Text("%.2f | %.2f | %.2f | %.2f", m[i * 4], m[i * 4 + 1], m[i * 4 + 2], m[i * 4 + 3]);
                }

                RECT rect;
                GetClientRect(game_hwnd, &rect);
                ImGui::Text("Window Size: %d x %d", rect.right, rect.bottom);
            }

            if (ImGui::BeginTabBar("MainTabs"))
            {
                if (ImGui::BeginTabItem("Aimbot"))
                {
                    ImGui::TextColored(ImVec4(1, 1, 0, 1), "Weapon Assistance");
                    ImGui::Checkbox("Enable Aimbot", &aimbotEnabled);
                    ImGui::Checkbox("Right Click Activation", &aimbotKeyEnabled);
                    ImGui::SliderFloat("FOV Radius", &aimbotFovRadius, 10.0f, 800.0f, "%.0f");
                    ImGui::Separator();
                    ImGui::Checkbox("Rapid Fire", &rapidFireEnabled);
                    ImGui::Checkbox("No Recoil", &noRecoilEnabled);
                    ImGui::Checkbox("No Spread", &noSpreadEnabled);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Visuals"))
                {
                    ImGui::Checkbox("Player ESP", &espEnabled);
                    if (espEnabled) {
                        ImGui::Indent();
                        ImGui::Checkbox("Names", &espNames);
                        ImGui::Checkbox("HP/Armor", &espHealthArmor);
                        ImGui::Unindent();
                    }
                    ImGui::Checkbox("Vehicle ESP", &vehicleEspEnabled);
                    ImGui::Checkbox("Radar Hack", &radarEnabled);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Player"))
                {
                    ImGui::Checkbox("GodMode", &godModeEnabled);
                    ImGui::Checkbox("AirBreak", &airBreakEnabled);
                    ImGui::Checkbox("SpeedHack", &speedHackEnabled);
                    if (ImGui::Button("Full Heal & Armor", ImVec2(-1, 25)) && localPlayer) {
                        localPlayer->SetHealth(100.0f);
                        localPlayer->SetArmour(100.0f);
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Teleport"))
                {
                    ImGui::BeginChild("TP_List");
                    if (pPlayerPool) {
                        for (int i = 0; i < 1004; i++) {
                            auto pRemote = pPlayerPool->GetPlayer(i);
                            if (pRemote && pRemote->m_pPed) {
                                if (ImGui::Button(pPlayerPool->GetName(i), ImVec2(-1, 20)))
                                    TeleportToPlayer((samp::CPed*)pRemote->m_pPed);
                            }
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            ImGui::Separator();
            if (ImGui::Button("EMERGENCY UNLOAD", ImVec2(-1, 30))) {
                MH_DisableHook(MH_ALL_HOOKS);
                show_menu = false;
            }

            ImGui::End();
            ImGui::PopStyleVar();
        }
    }

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    return Original_EndScene(pDevice);
}

void* GetVTableFunction(int index)
{
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return nullptr;
    D3DPRESENT_PARAMETERS d3dpp = { 0 };
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetDesktopWindow();
    IDirect3DDevice9* pDummyDevice = nullptr;
    if (pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice) == S_OK)
    {
        void** vTable = *(void***)pDummyDevice;
        void* func = vTable[index];
        pDummyDevice->Release();
        pD3D->Release();
        return func;
    }
    pD3D->Release();
    return nullptr;
}

DWORD WINAPI MainThread(LPVOID lpReserved)
{
    while (!GetModuleHandleA("d3d9.dll") || !GetModuleHandleA("samp.dll")) Sleep(500);
    MH_Initialize();
    MH_CreateHook(GetVTableFunction(42), &Hooked_EndScene, (LPVOID*)&Original_EndScene);
    MH_CreateHook(GetVTableFunction(16), &Hooked_Reset, (LPVOID*)&Original_Reset);
    MH_CreateHookApi(L"user32.dll", "SetCursorPos", &Hooked_SetCursorPos, (LPVOID*)&Original_SetCursorPos);
    MH_EnableHook(MH_ALL_HOOKS);
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}