#include "pch.h"
#include <iostream>
#include <windows.h>
#include <tlhelp32.h>
#include <string>

// Функция для получения PID процесса по имени
DWORD GetProcessIdByName(const wchar_t* processName) {
    DWORD processId = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W entry;
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshot, &entry)) {
            do {
                if (!_wcsicmp(entry.szExeFile, processName)) {
                    processId = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    return processId;
}

// Включение привилегий отладчика для обхода ограничений доступа
bool EnableDebugPrivilege() {
    HANDLE hToken;
    LUID luid;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;
    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        return false;
    }
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
        CloseHandle(hToken);
        return false;
    }
    CloseHandle(hToken);
    return true;
}

int main() {
    // Настройки (измените под вашу DLL и игру)
    const wchar_t* targetProcess = L"csgo.exe"; // Пример процесса
    const char* dllName = "cheat.dll";          // Имя вашей DLL

    char fullPath[MAX_PATH];
    GetFullPathNameA(dllName, MAX_PATH, fullPath, NULL);

    std::cout << "[+] Запуск инжектора..." << std::endl;

    if (!EnableDebugPrivilege()) {
        std::cout << "[!] Не удалось получить права отладчика. Запустите от имени Администратора." << std::endl;
    }

    DWORD processId = 0;
    while (processId == 0) {
        processId = GetProcessIdByName(targetProcess);
        std::cout << "[.] Ожидание процесса " << std::endl;
        Sleep(1000);
    }

    std::cout << "[+] Процесс найден (PID: " << processId << ")" << std::endl;

    // Открытие процесса с расширенными правами
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) {
        std::cout << "[!] Ошибка OpenProcess: " << GetLastError() << std::endl;
        return 1;
    }

    // Выделение памяти в целевом процессе под путь к DLL
    LPVOID allocatedMem = VirtualAllocEx(hProcess, NULL, strlen(fullPath) + 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!allocatedMem) {
        std::cout << "[!] Ошибка VirtualAllocEx" << std::endl;
        CloseHandle(hProcess);
        return 1;
    }

    // Запись пути к DLL в память игры
    if (!WriteProcessMemory(hProcess, allocatedMem, fullPath, strlen(fullPath) + 1, NULL)) {
        std::cout << "[!] Ошибка WriteProcessMemory" << std::endl;
        VirtualFreeEx(hProcess, allocatedMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }

    // Создание потока, который вызовет LoadLibraryA
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, allocatedMem, 0, NULL);
    if (!hThread) {
        std::cout << "[!] Ошибка CreateRemoteThread: " << GetLastError() << std::endl;
        VirtualFreeEx(hProcess, allocatedMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }

    std::cout << "[+] DLL успешно инжектирована!" << std::endl;

    // Очистка
    WaitForSingleObject(hThread, INFINITE);
    VirtualFreeEx(hProcess, allocatedMem, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    return 0;
}