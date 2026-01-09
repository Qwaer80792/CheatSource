#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <iostream>

#include "includes/imgui/imgui.h"
#include "includes/imgui/imgui_impl_dx9.h"
#include "includes/imgui/imgui_impl_win32.h"
#include "includes/minhook/MinHook.h"

#pragma comment(lib, "libs/directx/d3d9.lib")
#pragma comment(lib, "libs/directx/d3dx9.lib")
#pragma comment(lib, "libs/minhook/MinHook.x86.lib")


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

LONGLONG last_pc = 0;
LONGLONG frequency = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

BOOL WINAPI Hooked_SetCursorPos(int X, int Y) {
    if (show_menu) return TRUE;
    return Original_SetCursorPos(X, Y);
}

LRESULT CALLBACK Hooked_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (show_menu) {
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

        ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) return true;
    }
    return CallWindowProc(Original_WndProc, hWnd, msg, wParam, lParam);
}

HRESULT STDMETHODCALLTYPE Hooked_Reset(IDirect3DDevice9* pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (init) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = Original_Reset(pDevice, pPresentationParameters);
    if (SUCCEEDED(hr) && init) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

// --- Основной хук отрисовки ---
HRESULT STDMETHODCALLTYPE Hooked_EndScene(IDirect3DDevice9* pDevice) {
    // 1. Переключение меню (INSERT)
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

    LARGE_INTEGER current_pc;
    QueryPerformanceCounter(&current_pc);
    if (frequency == 0) QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
    double interval = (double)(current_pc.QuadPart - last_pc) / frequency;
    if (interval < 0.002) return Original_EndScene(pDevice);
    last_pc = current_pc.QuadPart;

    if (!init) {
        D3DDEVICE_CREATION_PARAMETERS cp;
        if (SUCCEEDED(pDevice->GetCreationParameters(&cp))) {
            game_hwnd = cp.hFocusWindow;
            if (game_hwnd) {
                ImGui::CreateContext();
                ImGui_ImplWin32_Init(game_hwnd);
                ImGui_ImplDX9_Init(pDevice);

                ImGuiIO& io = ImGui::GetIO();
                io.IniFilename = nullptr;
                io.MouseDrawCursor = true; 

                Original_WndProc = (WNDPROC)SetWindowLongPtr(game_hwnd, GWLP_WNDPROC, (LONG_PTR)Hooked_WndProc);
                init = true;
            }
        }
    }

    if (init && show_menu) {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
        ImGui::Begin("Radmir Internal Final v1.0", &show_menu);

        if (ImGui::CollapsingHeader("Player Hacks")) {
            static bool godmode = false;
            ImGui::Checkbox("GodMode (Visual)", &godmode);

            static float speed = 1.0f;
            ImGui::SliderFloat("Move Speed", &speed, 1.0f, 10.0f);
        }

        if (ImGui::Button("Unload Cheat")) {
        }

        ImGui::End();

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    return Original_EndScene(pDevice);
}

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

// --- Поток чита ---
DWORD WINAPI MainThread(LPVOID lpReserved) {
    while (!GetModuleHandleA("d3d9.dll")) Sleep(500);

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