#define _CRT_SECURE_NO_WARNINGS

#include "pch.h"
#include <Windows.h>
#include <cstdio>

static volatile bool g_bShouldRun = true;

// Простая функция лога для ядра чита
void CoreLog(const char* text)
{
    FILE* f = nullptr;
    if (fopen_s(&f, "core_debug.log", "a") == 0 && f)
    {
        fprintf(f, "%s", text);
        fclose(f);
    }
}

DWORD WINAPI MainCheatThread(LPVOID)
{
    Sleep(1000); // Ждем инициализацию игры

    HMODULE hGTA = GetModuleHandleA("gta_sa.exe");
    HMODULE hSAMP = GetModuleHandleA("samp.dll");

    // Логируем старт
    FILE* f = nullptr;
    if (fopen_s(&f, "core_debug.log", "a") == 0 && f)
    {
        fprintf(f, "[Core] Started. GTA: %p, SAMP: %p\n", hGTA, hSAMP);
        fclose(f);
    }

    // Главный цикл
    while (g_bShouldRun)
    {
        // Твой код чита тут

        // Тест выгрузки на кнопку END
        if (GetAsyncKeyState(VK_END) & 0x8000)
        {
            g_bShouldRun = false;
        }
        Sleep(50);
    }

    CoreLog("[Core] Thread finishing...\n");

    FreeLibraryAndExitThread(GetModuleHandleA("CheatCore.dll"), 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    static HANDLE hThread = NULL;

    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        CoreLog("[Core] DllMain Attached.\n");
        hThread = CreateThread(NULL, 0, MainCheatThread, NULL, 0, NULL);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        g_bShouldRun = false;
        if (hThread)
        {
            WaitForSingleObject(hThread, 1000);
            CloseHandle(hThread);
            hThread = NULL;
        }
        CoreLog("[Core] DllMain Detached.\n");
    }
    return TRUE;
}