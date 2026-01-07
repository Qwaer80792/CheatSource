#include "pch.h"
#include <windows.h>
#include <tlhelp32.h>
#include <string>

extern "C" __declspec(dllexport) int __stdcall Inject(const char* procName, const char* dllPath) {
    DWORD processId = 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 entry; 
        entry.dwSize = sizeof(entry);

        if (Process32First(snapshot, &entry)) {
            do {
                if (_stricmp(entry.szExeFile, procName) == 0) {
                    processId = entry.th32ProcessID;
                    break;
                }
            } while (Process32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }

    if (processId == 0) return 1; 

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
    if (!hProcess) return 2; 

    LPVOID alloc = VirtualAllocEx(hProcess, NULL, strlen(dllPath) + 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!alloc) {
        CloseHandle(hProcess);
        return 3;
    }

    if (!WriteProcessMemory(hProcess, alloc, dllPath, strlen(dllPath) + 1, NULL)) {
        VirtualFreeEx(hProcess, alloc, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 4;
    }

    FARPROC loadLibAddr = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)loadLibAddr, alloc, 0, NULL);

    if (!hThread) {
        VirtualFreeEx(hProcess, alloc, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 5;
    }

    WaitForSingleObject(hThread, 1000);
    VirtualFreeEx(hProcess, alloc, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return 0; 
}