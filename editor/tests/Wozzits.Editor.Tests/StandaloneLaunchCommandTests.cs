using System.Diagnostics;
using Wozzits.Editor.ViewModels;

namespace Wozzits.Editor.Tests;

/// <summary>
/// D3-P045. `ae29747` made LaunchStandaloneCommand's CanExecute depend on
/// IsStandaloneRunning -- correctly, to refuse a second play instead of orphaning
/// the first -- but that added a state transition nothing signalled.
/// NotifyCanExecuteChanged was raised only on LAUNCH; the launcher's own Exited
/// handler just logs. So running -> exited never announced itself, and whether the
/// user could ever play again depended on whether Avalonia happened to re-query
/// CanExecute when the Run submenu reattached.
/// </summary>
public sealed class StandaloneLaunchCommandTests
{
    // A real child process, because Process.Exited is the thing under test and it
    // only fires for a real one. `cmd /c exit` starts and exits promptly. Windows-
    // only, like the editor and the DX12 engine it drives.
    private static Process StartShortLivedProcess()
    {
        var process = new Process
        {
            StartInfo = new ProcessStartInfo("cmd", "/c exit 0")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
            },
            EnableRaisingEvents = true,
        };
        process.Start();
        return process;
    }

    [Fact]
    public async Task TheCommandAnnouncesItIsRunnableAgainWhenThePlayExits()
    {
        using var launched = new ManualResetEventSlim();
        var viewModel = new MainWindowViewModel(
            projectDirectory: Path.GetTempPath(),
            standaloneLauncher: (_, _) => StartShortLivedProcess());

        var announcements = 0;
        viewModel.LaunchStandaloneCommand.CanExecuteChanged += (_, _) => ++announcements;

        Assert.True(viewModel.LaunchStandaloneCommand.CanExecute(null));
        viewModel.LaunchStandaloneCommand.Execute(null);
        var afterLaunch = announcements;

        // Wait for the child to exit AND for the notification to arrive. Polling
        // CanExecute would not distinguish "the command re-evaluated" from "the
        // command announced", and it is the announcement that was missing.
        var deadline = DateTime.UtcNow.AddSeconds(20);
        while (announcements <= afterLaunch && DateTime.UtcNow < deadline)
        {
            await Task.Delay(25);
        }

        Assert.True(
            announcements > afterLaunch,
            "the play exited and the command never raised CanExecuteChanged");
        Assert.True(viewModel.LaunchStandaloneCommand.CanExecute(null));
    }

    // The control: the launch half still announces, and still refuses a second
    // play while the first is alive (the `ae29747` behaviour this must not undo).
    [Fact]
    public void TheCommandStillRefusesASecondPlayWhileTheFirstIsAlive()
    {
        using var block = new ManualResetEventSlim();
        var started = new List<Process>();
        var viewModel = new MainWindowViewModel(
            projectDirectory: Path.GetTempPath(),
            standaloneLauncher: (_, _) =>
            {
                var process = new Process
                {
                    StartInfo = new ProcessStartInfo("cmd", "/c pause")
                    {
                        UseShellExecute = false,
                        CreateNoWindow = true,
                        RedirectStandardInput = true,
                    },
                    EnableRaisingEvents = true,
                };
                process.Start();
                started.Add(process);
                return process;
            });

        try
        {
            viewModel.LaunchStandaloneCommand.Execute(null);

            Assert.False(viewModel.LaunchStandaloneCommand.CanExecute(null));
            Assert.Single(started);
        }
        finally
        {
            foreach (var process in started)
            {
                process.Kill(entireProcessTree: true);
                process.Dispose();
            }
        }
    }
}
