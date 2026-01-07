using Avalonia.Controls;
using Avalonia.Threading;
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace CheatUI;

public partial class MainWindow : Window
{
    [DllImport("InternalLoader.dll", CallingConvention = CallingConvention.StdCall, CharSet = CharSet.Ansi)]
    public static extern int Inject(string procName, string dllPath);

    public MainWindow()
    {
        InitializeComponent();
        var injectBtn = this.FindControl<Button>("InjectButton");
        if (injectBtn != null)
            injectBtn.Click += async (s, e) => await RunLoader();
    }

    private async Task RunLoader()
    {
        UpdateLog("Вызов InternalLoader...");

        await Task.Run(() =>
        {
            try
            {
                string targetDll = Path.GetFullPath("CheatCore.dll");

                if (!File.Exists("InternalLoader.dll"))
                {
                    UpdateLog("Ошибка: Инжектор (InternalLoader.dll) не найден!");
                    return;
                }

                int result = Inject("gta_sa.exe", targetDll);

                Dispatcher.UIThread.InvokeAsync(() => {
                    switch (result)
                    {
                        case 0: UpdateLog("УСПЕХ: CheatCore внедрен!"); break;
                        case 1: UpdateLog("Ошибка: gta_sa.exe не найден."); break;
                        case 2: UpdateLog("Ошибка: Нет прав доступа."); break;
                        default: UpdateLog($"Ошибка инжектора: {result}"); break;
                    }
                });
            }
            catch (Exception ex)
            {
                UpdateLog($"Ошибка вызова DLL: {ex.Message}");
            }
        });
    }

    private void UpdateLog(string msg) => Dispatcher.UIThread.InvokeAsync(() => {
        var log = this.FindControl<TextBlock>("LogText");
        if (log != null) log.Text = $"[{DateTime.Now:HH:mm:ss}] {msg}\n" + log.Text;
    });
}