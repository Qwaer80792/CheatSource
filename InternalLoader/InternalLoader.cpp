#define _CRT_SECURE_NO_WARNINGS

#include "pch.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>

// Вспомогательная функция для логов (убирает ошибку с "f")
void LogToFile(const char* format, const char* text, DWORD number = 0)
{
    FILE* f = nullptr;
    // Пишем в injector.log рядом с exe или в рабочую папку
    if (fopen_s(&f, "injector_debug.log", "a") == 0 && f)
    {
        fprintf(f, format, text, number);
        fclose(f);
    }
}

DWORD WINAPI LoadCheatCore(LPVOID lpParam)
{
    HMODULE hMyDll = (HMODULE)lpParam;
    char pathBuffer[MAX_PATH];

    // 1. Получаем путь к DLL
    if (GetModuleFileNameA(hMyDll, pathBuffer, MAX_PATH) == 0)
    {
        strcpy_s(pathBuffer, "CheatCore.dll");
    }
    else
    {
        char* lastSlash = strrchr(pathBuffer, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
        strcat_s(pathBuffer, MAX_PATH, "CheatCore.dll");
    }

    LogToFile("[Loader] Target path: %s\n", pathBuffer);

    // 2. Загружаем чит
    HMODULE hCore = LoadLibraryA(pathBuffer);

    if (hCore)
    {
        // %p требует void*, приводим hCore
        LogToFile("[Loader] CheatCore loaded SUCCESS at address: %p\n", "OK", (DWORD)hCore);
    }
    else
    {
        LogToFile("[Loader] FAILED to load. Error code: %lu\n", "ERROR", GetLastError());
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        // Передаем hModule, чтобы найти путь
        CreateThread(NULL, 0, LoadCheatCore, (LPVOID)hModule, 0, NULL);
    }
    return TRUE;
}