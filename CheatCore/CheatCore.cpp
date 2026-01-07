#include "pch.h"
#include <Windows.h>

DWORD WINAPI MainThread(LPVOID lpParam) {
    while (GetModuleHandleA("samp.dll") == NULL) Sleep(500);

    MessageBoxA(NULL, "Cheat Core Active!", "Success", MB_OK | MB_ICONINFORMATION);

    while (true) {
        if (GetAsyncKeyState(VK_DELETE) & 0x8000) break;
        Sleep(100);
    }

    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, MainThread, hModule, 0, NULL);
    }
    return TRUE;
}