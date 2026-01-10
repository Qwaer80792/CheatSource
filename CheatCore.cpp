#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NO_STRICT

#include <windows.h>
#include <objbase.h>

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

// Библиотеки C++
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string> 

// ImGui
#include "includes/imgui/imgui.h"
#include "includes/imgui/imgui_impl_dx9.h"
#include "includes/imgui/imgui_impl_win32.h"

// MinHook
#include "MinHook.h"

// SAMP-API (Версия R3-1) 
/*
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
*/

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


namespace samp = sampapi::v037r3;


// --- Типы функций ---
typedef HRESULT(STDMETHODCALLTYPE* EndScene_t)(IDirect3DDevice9*);
typedef HRESULT(STDMETHODCALLTYPE* Reset_t)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
typedef BOOL(WINAPI* SetCursorPos_t)(int, int);

// --- Глобальные переменные ---
EndScene_t Original_EndScene = nullptr;
Reset_t Original_Reset = nullptr;
WNDPROC Original_WndProc = nullptr;
SetCursorPos_t Original_SetCursorPos = nullptr;

bool init = false;
bool show_menu = true;
HWND game_hwnd = nullptr;

// --- Переменные для функций ---
bool espEnabled = true;
bool espNames = true;
bool espHealthArmor = true;
bool aimbotEnabled = false;
bool aimbotKeyEnabled = true;
float aimbotRange = 100.0f;
float aimbotFovRadius = 150.0f;
bool rapidFireEnabled = false;
bool noRecoilEnabled = false;
bool noSpreadEnabled = false;
bool godModeEnabled = false;
bool radarEnabled = false;
float radarZoom = 100.0f; // Это будет масштаб
bool speedHackEnabled = false;
float speedMultiplier = 2.0f;
bool vehicleEspEnabled = false;
bool autoShootEnabled = false;
bool autoShootKeyEnabled = true;

// --- Хук на перемещение курсора игрой ---
BOOL WINAPI Hooked_SetCursorPos(int X, int Y) {
    if (show_menu) return TRUE;
    return Original_SetCursorPos(X, Y);
}

// --- Обработка ввода (WndProc) ---
LRESULT CALLBACK Hooked_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (show_menu) {

        if (ImGui_ImplWin32_WndProcHandler) {
            ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        }
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) return true;
    }
    return CallWindowProc(Original_WndProc, hWnd, msg, wParam, lParam);
}

// --- Хук Reset ---
HRESULT STDMETHODCALLTYPE Hooked_Reset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (init) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = Original_Reset(pDevice, pPresentationParameters);
    if (SUCCEEDED(hr) && init) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

samp::CPed* GetLocalPlayer() {
    auto netGame = samp::RefNetGame();
    if (!netGame || !netGame->m_pPools || !netGame->m_pPools->m_pPlayer) return nullptr;

    auto playerPool = netGame->m_pPools->m_pPlayer;

    if (!playerPool->m_localInfo.m_pObject || !playerPool->m_localInfo.m_pObject->m_pPed)
        return nullptr;

    return playerPool->m_localInfo.m_pObject->m_pPed;
}

std::vector<samp::CPed*> GetPlayers() {
    std::vector<samp::CPed*> players;
    auto netGame = samp::RefNetGame();
    if (!netGame || !netGame->m_pPools || !netGame->m_pPools->m_pPlayer) return players;

    auto pPool = netGame->m_pPools->m_pPlayer;

    for (int i = 0; i < 1004; i++) {
        if (pPool->IsConnected(i)) {
            auto pRemotePlayer = pPool->GetPlayer(i);

            if (pRemotePlayer) {
                auto pPed = pRemotePlayer->m_pPed;
                if (pPed) {
                    players.push_back(pPed);
                }
            }
        }
    }
    return players;
}

std::vector<samp::CVehicle*> GetVehicles() {
    std::vector<samp::CVehicle*> vehicles;

    auto netGame = samp::RefNetGame();
    if (!netGame || !netGame->m_pPools) return vehicles;

    auto vehiclePool = netGame->m_pPools->m_pVehicle;
    if (!vehiclePool) return vehicles;

    for (int i = 0; i < samp::CVehiclePool::MAX_VEHICLES; i++) {
        if (vehiclePool->m_pObject[i]) {
            auto pVehicle = vehiclePool->m_pObject[i];
            if (pVehicle) {
                vehicles.push_back(pVehicle);
            }
        }
    }
    return vehicles;
}

bool WorldToScreen(const sampapi::CVector& worldPos, sampapi::CVector& screenPos, int screenWidth, int screenHeight) {
    float* viewMatrix = (float*)0xB6FA2C;

    float screenW = viewMatrix[3] * worldPos.x + viewMatrix[7] * worldPos.y + viewMatrix[11] * worldPos.z + viewMatrix[15];

    if (screenW < 0.01f) return false;
    float screenX = viewMatrix[0] * worldPos.x + viewMatrix[4] * worldPos.y + viewMatrix[8] * worldPos.z + viewMatrix[12];
    float screenY = viewMatrix[1] * worldPos.x + viewMatrix[5] * worldPos.y + viewMatrix[9] * worldPos.z + viewMatrix[13];

    float camX = screenWidth / 2.0f;
    float camY = screenHeight / 2.0f;

    screenPos.x = camX + (camX * screenX / screenW);
    screenPos.y = camY - (camY * screenY / screenW);

    return true;
}

void DrawPlayerESP(samp::CPed* player, int screenWidth, int screenHeight) {
    if (!player || !player->m_pGamePed) return;

    sampapi::CVector worldPos;
    player->GetBonePosition(1, &worldPos);

    sampapi::CVector screenPos;
    if (WorldToScreen(worldPos, screenPos, screenWidth, screenHeight)) {
        auto pNetGame = samp::RefNetGame();
        auto pPool = (pNetGame && pNetGame->m_pPools) ? pNetGame->m_pPools->m_pPlayer : nullptr;

        if (espNames && pPool) {
            sampapi::ID nId = pPool->Find(player->m_pGamePed);
            if (nId != -1) {
                const char* szName = pPool->GetName(nId);
                if (szName) {
                    ImGui::GetBackgroundDrawList()->AddText(
                        ImVec2(screenPos.x, screenPos.y - 40),
                        IM_COL32(255, 255, 255, 255),
                        szName
                    );
                }
            }
        }

        if (espHealthArmor) {
            float health = player->GetHealth();
            float armor = player->GetArmour();

            float healthWidth = 50.0f * (fminf(health, 100.0f) / 100.0f);
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                ImVec2(screenPos.x - 25, screenPos.y + 10),
                ImVec2(screenPos.x - 25 + healthWidth, screenPos.y + 15),
                IM_COL32(0, 255, 0, 255)
            );
        }
    }
}

void Aimbot(samp::CPed* localPlayer, std::vector<samp::CPed*>& players) {
    if (!aimbotEnabled || !localPlayer || !localPlayer->m_pGamePed) return;

    float closestDistance = FLT_MAX;
    samp::CPed* bestTarget = nullptr;

    sampapi::CVector localPos;
    localPlayer->GetBonePosition(1, &localPos);

    for (auto& player : players) {
        if (!player || player == localPlayer || player->IsDead()) continue;

        sampapi::CVector targetPos;
        player->GetBonePosition(8, &targetPos); 

        float dx = targetPos.x - localPos.x;
        float dy = targetPos.y - localPos.y;
        float dz = targetPos.z - localPos.z;
        float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (distance < closestDistance && distance < aimbotRange) { 
            closestDistance = distance;
            bestTarget = player;
        }
    }

    if (bestTarget && (!aimbotKeyEnabled || (GetAsyncKeyState(VK_RBUTTON) & 0x8000))) {
        sampapi::CVector headPos;
        bestTarget->GetBonePosition(8, &headPos);

        float relX = headPos.x - localPos.x;
        float relY = headPos.y - localPos.y;
        float relZ = headPos.z - localPos.z;
        float dist2D = std::sqrt(relX * relX + relY * relY);

        float yaw = std::atan2(relY, relX) - (3.14159265f / 2.0f); 
        float pitch = std::atan2(relZ, dist2D); 
        *(float*)0xB6F258 = yaw;
        *(float*)0xB6F248 = pitch;
    }
}

void NoRecoil() {
    if (!noRecoilEnabled) return;
    *(float*)0xB70150 = 0.0f;

    sampapi::v037r3::AimStuff::Aim& localAim = sampapi::v037r3::AimStuff::RefLocalPlayerAim();
    sampapi::v037r3::AimStuff::UpdateAim();
}

void NoSpread() {
    if (!noSpreadEnabled) return;
    sampapi::v037r3::AimStuff::UpdateAim();
    sampapi::v037r3::AimStuff::ApplyAim();
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

void AutoShoot(samp::CPed* localPlayer, std::vector<samp::CPed*>& players, int screenWidth, int screenHeight) {
    if (!autoShootEnabled || !localPlayer || !localPlayer->m_pGamePed) return;

    for (auto& player : players) {
        if (!player || player == localPlayer || player->IsDead()) continue;

        sampapi::CVector worldPos;
        player->GetBonePosition(3, &worldPos);

        sampapi::CVector screenPos;
        if (WorldToScreen(worldPos, screenPos, screenWidth, screenHeight)) {
            float centerX = screenWidth / 2.0f;
            float centerY = screenHeight / 2.0f;

            if (abs(screenPos.x - centerX) < 20 && abs(screenPos.y - centerY) < 20) {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                break; 
            }
        }
    }
}

void RapidFire() {
    if (!rapidFireEnabled) return;

    samp::CGame* pGame = samp::RefGame();
    if (!pGame || !pGame->m_pPlayerPed) return;

    int weaponId = pGame->m_pPlayerPed->GetCurrentWeapon();
    if (weaponId < 16) return; 

    void* pWeaponInfo = (void*)pGame->GetWeaponInfo(weaponId, 2);

    if (pWeaponInfo && (GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {

        uintptr_t addr = reinterpret_cast<uintptr_t>(pWeaponInfo);
        *(float*)(addr + 0x60) = 0.1f; 
    }
}

void ApplyNoRecoil() {
    if (!noRecoilEnabled) return;

    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
        *(float*)0xB6EC14 = 0.0f;
        *(float*)0xB6EC18 = 0.0f;
    }
}

void TeleportToPlayer(samp::CPed* target) {
    if (!target || !target->m_pGameEntity) return;

    samp::CGame* pGame = samp::RefGame();
    if (!pGame || !pGame->m_pPlayerPed) return;

    sampapi::CMatrix targetMatrix;
    target->GetMatrix(&targetMatrix);

    sampapi::CVector targetPos = targetMatrix.pos;

    targetPos.z += 1.0f;
    pGame->m_pPlayerPed->Teleport(targetPos);
}

void DrawRadar(samp::CPed* localPlayer, std::vector<samp::CPed*>& players, int screenWidth, int screenHeight) {
    if (!radarEnabled || !localPlayer) return;

    ImGui::SetNextWindowSize(ImVec2(200, 200), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Radar", &radarEnabled, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetWindowPos();
    ImVec2 winSize = ImGui::GetWindowSize();
    ImVec2 radarCenter = ImVec2(winPos.x + winSize.x / 2, winPos.y + winSize.y / 2);

    drawList->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y), IM_COL32(30, 30, 30, 150));
    drawList->AddCircleFilled(radarCenter, 4.0f, IM_COL32(0, 255, 0, 255)); // Мы

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

void DrawVehicleESP(samp::CVehicle* vehicle, int screenWidth, int screenHeight) {
    if (!vehicleEspEnabled || !vehicle || !vehicle->m_pGameEntity) return;

    sampapi::CMatrix vehMatrix;
    vehicle->GetMatrix(&vehMatrix);
    sampapi::CVector worldPos = vehMatrix.pos;

    sampapi::CVector screenPos;
    if (WorldToScreen(worldPos, screenPos, screenWidth, screenHeight)) {

        int modelId = vehicle->GetModelIndex();

        char modelStr[64];
        sprintf(modelStr, "Vehicle: %d", modelId);

        ImGui::GetBackgroundDrawList()->AddText(
            ImVec2(screenPos.x, screenPos.y),
            IM_COL32(255, 255, 0, 255),
            modelStr
        );
    }
}

// --- БЛОК ХУКА ОТРИСОВКИ ---
HRESULT STDMETHODCALLTYPE Hooked_EndScene(IDirect3DDevice9* pDevice) {
    static bool init = false;
    sampapi::v037r3::CPed* localPlayer = nullptr;

    static std::vector<sampapi::v037r3::CPed*> players;
    static std::vector<sampapi::v037r3::CVehicle*> vehicles;

    players.clear();
    vehicles.clear();

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
                init = true;
            }
        }
    }

    auto pNetGame = sampapi::v037r3::RefNetGame();
    auto pGame = sampapi::v037r3::RefGame();

    ImGuiIO& io = ImGui::GetIO();
    int screenWidth = (int)io.DisplaySize.x;
    int screenHeight = (int)io.DisplaySize.y;

    if (pNetGame && pNetGame->m_pPools) {
        if (pGame && pGame->m_pPlayerPed) {
            localPlayer = (sampapi::v037r3::CPed*)pGame->m_pPlayerPed;
        }

        auto pPlayerPool = pNetGame->m_pPools->m_pPlayer;
        if (pPlayerPool) {
            for (int i = 0; i < 1004; i++) {
                auto pInfo = pPlayerPool->m_pObject[i];
                if (pInfo && pInfo->m_pPlayer && pInfo->m_pPlayer->m_pPed) {
                    players.push_back((sampapi::v037r3::CPed*)pInfo->m_pPlayer->m_pPed);
                }
            }
        }

        auto pVehiclePool = pNetGame->m_pPools->m_pVehicle;
        if (pVehiclePool) {
            for (int i = 0; i < 2000; i++) {
                auto pSAMPVehicle = pVehiclePool->m_pObject[i];
                if (pSAMPVehicle && pSAMPVehicle->m_pGameVehicle) {
                    vehicles.push_back((sampapi::v037r3::CVehicle*)pSAMPVehicle->m_pGameVehicle);
                }
            }
        }

        if (localPlayer) {
            Aimbot(localPlayer, players);
            ApplyNoRecoil();
            NoSpread();
            GodMode();
            SpeedHack();
            RapidFire();
            AutoShoot(localPlayer, players, screenWidth, screenHeight);
        }

        if (espEnabled) {
            for (auto player : players) DrawPlayerESP(player, screenWidth, screenHeight);
        }
        if (vehicleEspEnabled) {
            for (auto vehicle : vehicles) DrawVehicleESP(vehicle, screenWidth, screenHeight);
        }
        if (localPlayer && radarEnabled) {
            DrawRadar(localPlayer, players, screenWidth, screenHeight);
        }

        if (show_menu) {
            io.MouseDrawCursor = true;
            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGui::Begin("Radmir Internal Final v3.5", &show_menu);

            if (ImGui::CollapsingHeader("ESP")) {
                ImGui::Checkbox("Enable ESP", &espEnabled);
            }
            if (ImGui::CollapsingHeader("Teleport")) {
                if (pPlayerPool) {
                    for (int i = 0; i < 1004; i++) {
                        auto pInfo = pPlayerPool->m_pObject[i];
                        if (pInfo && pInfo->m_pPlayer && pInfo->m_pPlayer->m_pPed) {
                            if (ImGui::Button(pInfo->m_szNick.c_str())) {
                                TeleportToPlayer((sampapi::v037r3::CPed*)pInfo->m_pPlayer->m_pPed);
                            }
                        }
                    }
                }
            }

            if (ImGui::Button("Unload")) { /* Logic */ }
            ImGui::End();
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        }
        else {
            io.MouseDrawCursor = false;
        }
    }

    return Original_EndScene(pDevice);
}

// --- ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ---
void* GetVTableFunction(int index) {
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS d3dpp = { 0 };
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetDesktopWindow();
    IDirect3DDevice9* pDummyDevice = nullptr;
    if (pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice) == S_OK) {
        void** vTable = *(void***)pDummyDevice;
        void* func = vTable[index];
        pDummyDevice->Release();
        pD3D->Release();
        return func;
    }
    return nullptr;
}

DWORD WINAPI MainThread(LPVOID lpReserved) {
    while (!GetModuleHandleA("d3d9.dll") || !GetModuleHandleA("samp.dll")) Sleep(500);
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