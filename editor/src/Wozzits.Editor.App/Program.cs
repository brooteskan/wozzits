using Avalonia;
using System;
using Wozzits.Editor.Core.Logging;

namespace Wozzits.Editor.App;

sealed class Program
{
    // Initialization code. Don't use any Avalonia, third-party APIs or any
    // SynchronizationContext-reliant code before AppMain is called: things aren't initialized
    // yet and stuff might break.
    [STAThread]
    public static void Main(string[] args)
    {
        // FIRST, before Avalonia exists, so a failure during startup is recorded
        // too. CrashGuard touches nothing but the BCL, which is why it can run
        // this early. The UI-thread half is installed in App.Initialize, since it
        // needs a dispatcher.
        CrashGuard.Install();

        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    // Avalonia configuration, don't remove; also used by visual designer.
    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
#if DEBUG
            .WithDeveloperTools()
#endif
            .WithInterFont()
            .LogToTrace();
}
