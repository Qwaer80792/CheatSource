using Avalonia;
using Avalonia.Controls;
using Avalonia.Threading;
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace CheatUI;

public partial class MainWindow : Window
{
    // Используем W-версии функций (Unicode) для корректной работы с путями
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr GetModuleHandleW(string lpModuleName);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, uint nSize, out IntPtr lpNumberOfBytesWritten);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetExitCodeThread(IntPtr hThread, out uint lpExitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr hObject);

    const uint PROCESS_ALL_ACCESS = 0x1F0FFF;
    const uint MEM_COMMIT = 0x1000;
    const uint MEM_RESERVE = 0x2000;
    const uint PAGE_READWRITE = 0x04;

    public MainWindow()
    {
        InitializeComponent();
        SetupEventHandlers();
    }

    private void SetupEventHandlers()
    {
        var injectButton = this.FindControl<Button>("InjectButton");
        if (injectButton != null)
        {
            injectButton.Click += async (s, e) => {
                injectButton.IsEnabled = false;
                await Task.Run(() => InjectLogic());
                injectButton.IsEnabled = true;
            };
        }
    }

    private void InjectLogic()
    {
        string processName = "gta_sa";
        string dllName = "InternalLoader.dll";

        // 1. Получаем абсолютный путь и проверяем его
        string fullPath = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, dllName));

        UpdateLog($"[.] Target: {processName}.exe");
        UpdateLog($"[.] DLL Path: {fullPath}");

        Process[] processes = Process.GetProcessesByName(processName);
        if (processes.Length == 0)
        {
            UpdateLog("[-] Error: Process not found!");
            return;
        }

        IntPtr hProc = OpenProcess(PROCESS_ALL_ACCESS, false, processes[0].Id);
        if (hProc == IntPtr.Zero)
        {
            UpdateLog($"[-] OpenProcess failed! Error: {Marshal.GetLastWin32Error()}");
            return;
        }

        // 2. Используем LoadLibraryW для поддержки любых путей (включая кириллицу)
        // Путь в Unicode занимает 2 байта на символ + null-terminator
        byte[] pathBytes = Encoding.Unicode.GetBytes(fullPath + "\0");
        IntPtr mem = VirtualAllocEx(hProc, IntPtr.Zero, (uint)pathBytes.Length, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        if (WriteProcessMemory(hProc, mem, pathBytes, (uint)pathBytes.Length, out _))
        {
            // Берем LoadLibraryW (Unicode версия)
            IntPtr loadLibAddr = GetProcAddress(GetModuleHandleW("kernel32.dll"), "LoadLibraryW");
            IntPtr hThread = CreateRemoteThread(hProc, IntPtr.Zero, 0, loadLibAddr, mem, 0, IntPtr.Zero);

            if (hThread != IntPtr.Zero)
            {
                UpdateLog("[+] Remote thread created. Waiting...");
                System.Threading.Thread.Sleep(2000); // Даем время на загрузку

                uint exitCode;
                GetExitCodeThread(hThread, out exitCode);

                if (exitCode == 0)
                {
                    UpdateLog("[-] LoadLibraryW returned NULL (0x0). Injection failed.");
                    UpdateLog("[!] Check if DLL is x86 and has all dependencies (/MT).");
                }
                else
                {
                    UpdateLog($"[+] Success! DLL Loaded at 0x{exitCode:X}");
                }
                CloseHandle(hThread);
            }
        }
        CloseHandle(hProc);
    }

    private void UpdateLog(string msg) => Dispatcher.UIThread.InvokeAsync(() => {
        var log = this.FindControl<TextBlock>("LogText");
        if (log != null) log.Text = $"[{DateTime.Now:HH:mm:ss}] {msg}\n{log.Text}";
    });
}