using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace CheatUI;

public partial class MainWindow : Window
{
    // WinAPI импорты для инъекции
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    static extern IntPtr GetModuleHandle(string lpModuleName);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr VirtualAllocEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool WriteProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, uint nSize, out IntPtr lpNumberOfBytesWritten);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateRemoteThread(IntPtr hProcess, IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);

    const uint PROCESS_ALL_ACCESS = 0x1F0FFF;
    const uint MEM_COMMIT = 0x1000;
    const uint MEM_RESERVE = 0x2000;
    const uint PAGE_READWRITE = 0x04;

    public MainWindow()
    {
        InitializeComponent();
        SetupEventHandlers();
    }

    private void InitializeComponent()
    {
        AvaloniaXamlLoader.Load(this);
    }

    private void SetupEventHandlers()
    {
        var injectButton = this.FindControl<Button>("InjectButton");
        var statusText = this.FindControl<TextBlock>("StatusText");
        var logText = this.FindControl<TextBlock>("LogText");

        if (injectButton != null)
        {
            injectButton.Click += async (s, e) =>
            {
                try
                {
                    AddLog("Starting injection...");
                    statusText.Text = "Status: Injecting...";

                    string targetProcess = "gta_sa";
                    string dllPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "InternalLoader.dll");

                    if (!File.Exists(dllPath))
                    {
                        AddLog($"ERROR: DLL not found at {dllPath}");
                        statusText.Text = "Status: ❌ DLL not found!";
                        return;
                    }

                    bool success = InjectDLL(targetProcess, dllPath);

                    if (success)
                    {
                        AddLog("✅ Injection successful!");
                        statusText.Text = "Status: ✅ Injected!";
                    }
                    else
                    {
                        AddLog("❌ Injection failed!");
                        statusText.Text = "Status: ❌ Failed!";
                    }
                }
                catch (Exception ex)
                {
                    AddLog($"ERROR: {ex.Message}");
                    statusText.Text = "Status: ❌ Error!";
                }
            };
        }
    }

    private bool InjectDLL(string processName, string dllPath)
    {
        try
        {
            AddLog($"Looking for process: {processName}");

            // Ищем процесс GTA
            Process[] processes = Process.GetProcessesByName(processName);
            if (processes.Length == 0)
            {
                AddLog($"Process {processName} not found!");
                return false;
            }

            Process targetProcess = processes[0];
            AddLog($"Found process: {targetProcess.ProcessName} (PID: {targetProcess.Id})");

            // Открываем процесс
            IntPtr hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, targetProcess.Id);
            if (hProcess == IntPtr.Zero)
            {
                AddLog($"Failed to open process. Error: {Marshal.GetLastWin32Error()}");
                return false;
            }

            AddLog("Process opened successfully");

            // Выделяем память в процессе
            IntPtr allocatedMem = VirtualAllocEx(hProcess, IntPtr.Zero,
                (uint)(dllPath.Length + 1), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

            if (allocatedMem == IntPtr.Zero)
            {
                AddLog($"Failed to allocate memory. Error: {Marshal.GetLastWin32Error()}");
                CloseHandle(hProcess);
                return false;
            }

            AddLog($"Memory allocated at: 0x{allocatedMem:X}");

            // Пишем путь к DLL в память
            byte[] dllPathBytes = Encoding.ASCII.GetBytes(dllPath);
            bool writeResult = WriteProcessMemory(hProcess, allocatedMem, dllPathBytes,
                (uint)dllPathBytes.Length, out _);

            if (!writeResult)
            {
                AddLog($"Failed to write memory. Error: {Marshal.GetLastWin32Error()}");
                VirtualFreeEx(hProcess, allocatedMem, 0, 0x8000); // MEM_RELEASE
                CloseHandle(hProcess);
                return false;
            }

            AddLog("DLL path written to memory");

            // Получаем адрес LoadLibraryA
            IntPtr kernel32 = GetModuleHandle("kernel32.dll");
            IntPtr loadLibraryAddr = GetProcAddress(kernel32, "LoadLibraryA");

            AddLog($"LoadLibraryA address: 0x{loadLibraryAddr:X}");

            // Создаём удалённый поток
            IntPtr hThread = CreateRemoteThread(hProcess, IntPtr.Zero, 0,
                loadLibraryAddr, allocatedMem, 0, IntPtr.Zero);

            if (hThread == IntPtr.Zero)
            {
                AddLog($"Failed to create thread. Error: {Marshal.GetLastWin32Error()}");
                VirtualFreeEx(hProcess, allocatedMem, 0, 0x8000);
                CloseHandle(hProcess);
                return false;
            }

            AddLog("Remote thread created");

            // Ждём завершения
            WaitForSingleObject(hThread, 5000);

            // Закрываем дескрипторы
            CloseHandle(hThread);
            VirtualFreeEx(hProcess, allocatedMem, 0, 0x8000);
            CloseHandle(hProcess);

            AddLog("Injection completed");
            return true;
        }
        catch (Exception ex)
        {
            AddLog($"Exception in InjectDLL: {ex.Message}");
            return false;
        }
    }

    private void AddLog(string message)
    {
        var logText = this.FindControl<TextBlock>("LogText");
        if (logText != null)
        {
            logText.Text = $"[{DateTime.Now:HH:mm:ss}] {message}\n{logText.Text}";
        }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool VirtualFreeEx(IntPtr hProcess, IntPtr lpAddress, uint dwSize, uint dwFreeType);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool CloseHandle(IntPtr hObject);
}