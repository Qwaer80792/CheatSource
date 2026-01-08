#include <windows.h> 
#include <d3d9.h>
#include <d3dx9.h>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "MinHook.h"

typedef HRESULT(STDMETHODCALLTYPE* EndScene_t)(IDirect3DDevice9*);
EndScene_t Original_EndScene = nullptr;

bool init = false;
bool show_menu = true;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
WNDPROC Original_WndProc = nullptr;

LRESULT CALLBACK Hooked_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (show_menu && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    return CallWindowProc(Original_WndProc, hWnd, msg, wParam, lParam);
}

HRESULT STDMETHODCALLTYPE Hooked_EndScene(IDirect3DDevice9* pDevice) {
    if (!init) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();

        D3DDEVICE_CREATION_PARAMETERS cp;
        pDevice->GetCreationParameters(&cp);

        Original_WndProc = (WNDPROC)SetWindowLongPtr(cp.hFocusWindow, GWLP_WNDPROC, (LONG_PTR)Hooked_WndProc);

        ImGui_ImplWin32_Init(cp.hFocusWindow);
        ImGui_ImplDX9_Init(pDevice);
        init = true;
    }

    if (show_menu) {
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Cheat Project v1.0", &show_menu);
        ImGui::Text("Welcome to the Internal Menu");
        if (ImGui::Button("Unload DLL")) {
        }
        ImGui::End();

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    return Original_EndScene(pDevice);
}

DWORD WINAPI MainThread(LPVOID lpReserved) {
    while (!GetModuleHandleA("d3d9.dll")) Sleep(100);

    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return FALSE;

    D3DPRESENT_PARAMETERS d3dpp = { 0 };
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetForegroundWindow();

    IDirect3DDevice9* pDummyDevice = nullptr;
    if (pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice) != D3D_OK) {
        pD3D->Release();
        return FALSE;
    }

    void** vTable = *(void***)pDummyDevice;

    MH_Initialize();
    MH_CreateHook(vTable[42], &Hooked_EndScene, (LPVOID*)&Original_EndScene);
    MH_EnableHook(vTable[42]);

    pDummyDevice->Release();
    pD3D->Release();

    while (true) {
        if (GetAsyncKeyState(VK_INSERT) & 1) {
            show_menu = !show_menu;
        }
        Sleep(10);
    }

    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}