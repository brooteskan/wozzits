using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Wozzits.Editor.Core.Logging;
using Wozzits.Editor.Core.Projects;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.App.Views;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels;

namespace Wozzits.Editor.App;

public partial class App : Application
{
    public override void Initialize()
    {
        // The half of the crash guard that needs a dispatcher, and the half that
        // actually keeps the window alive (D3-Q1: log and continue). Marking the
        // exception Handled is what stops Avalonia tearing the process down.
        //
        // This is the hook that covers the six `async void`-shaped handlers --
        // an exception in one is posted to the UI dispatcher, and four of them
        // live in Views/EditorPanes/ with no route to the editor log at all, so
        // until now they were silent AND fatal.
        //
        // Handled means the operation that threw was abandoned partway and the
        // editor may be inconsistent. That is the accepted trade: a live window
        // the user can save from beats a dead one, and CrashGuard writes down
        // what happened so the state is not a mystery.
        Dispatcher.UIThread.UnhandledException += (_, args) =>
        {
            CrashGuard.Report(
                "Dispatcher.UIThread.UnhandledException",
                args.Exception,
                survived: true);
            args.Handled = true;
        };

        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.MainWindow = CreateStartupWindow(desktop);
        }

        base.OnFrameworkInitializationCompleted();
    }

    private static Window CreateStartupWindow(IClassicDesktopStyleApplicationLifetime desktop)
    {
        var engine = new WozzitsEngineNativeClient();
        var launchOptions = ProjectLaunchOptions.FromCommandLine(desktop.Args ?? []);

        if (launchOptions.ProjectDirectory is null)
        {
            return CreateProjectDirectoryPickerWindow(desktop);
        }

        var projectDirectory = new ProjectDirectory(launchOptions.ProjectDirectory);
        return CreateProjectWindow(desktop, projectDirectory, engine);
    }

    private static Window CreateProjectDirectoryPickerWindow(IClassicDesktopStyleApplicationLifetime desktop)
    {
        var pickerWindow = new ProjectDirectoryPickerWindow();
        pickerWindow.Opened += async (_, _) =>
        {
            var selectedFolders = await pickerWindow.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
            {
                Title = "Open Wozzits Project Directory",
                AllowMultiple = false,
            });

            var selectedFolder = selectedFolders.Count > 0 ? selectedFolders[0] : null;
            var selectedPath = selectedFolder?.TryGetLocalPath();

            if (selectedPath is null)
            {
                desktop.Shutdown();
                return;
            }

            var projectDirectory = new ProjectDirectory(selectedPath);
            var engine = new WozzitsEngineNativeClient();
            var projectWindow = CreateProjectWindow(desktop, projectDirectory, engine);

            desktop.MainWindow = projectWindow;
            projectWindow.Show();
            pickerWindow.Close();
        };

        return pickerWindow;
    }

    private static Window CreateProjectWindow(
        IClassicDesktopStyleApplicationLifetime desktop,
        ProjectDirectory projectDirectory,
        WozzitsEngineNativeClient engine)
    {
        var editorLog = new EditorLogBuffer();
        // Point the crash guard at the console now that one exists. It is mirrored
        // to FileLogSink, so a report lands both where the user can see it and
        // where it survives -- in order with everything around it, which is worth
        // more than a separate crash file. Before this, reports go to the log
        // directory directly (the folder picker runs before any of it).
        CrashGuard.SetSink(editorLog.AppendLine);
        editorLog.AppendLine($"[editor] Opening project: {projectDirectory.FullPath}");
        // Name the engine DLL actually loaded (path + build time + abi version) so a
        // stale / wrong-config wozzits_abi.dll is obvious here rather than as a
        // silent "my change didn't take".
        editorLog.AppendLine(WozzitsEngineNativeClient.DescribeLoadedAbi());

        var projectSnapshot = engine.LoadProjectSnapshot(projectDirectory.FullPath);

        if (projectSnapshot.IsValid)
        {
            editorLog.AppendLine($"[editor] Project loaded: {projectSnapshot.ProjectName}");
            // IsValid means the MANIFEST parsed -- the scene and asset graph load
            // separately and can each fail without touching it. The engine
            // computes a reason for either failure and used to hand it over
            // unread, so a project whose scene.json was corrupt, missing, or on
            // an unrecognized schema opened looking healthy with an empty tree
            // and no explanation anywhere (A3-C5, #77). Opening anyway is
            // deliberate: refusing would leave no way to open a project in order
            // to repair it. Say so instead.
            if (!projectSnapshot.Scene.Ok)
            {
                editorLog.AppendLine(
                    "[editor] WARNING: the scene did not load, so the tree is "
                    + $"empty: {projectSnapshot.Scene.Error}");
            }
            if (!projectSnapshot.AssetGraph.Ok)
            {
                editorLog.AppendLine(
                    "[editor] WARNING: the asset graph did not load, so nothing "
                    + $"will resolve or render: {projectSnapshot.AssetGraph.Error}");
            }
            // Per-run file mirror of the whole console (engine + editor + play
            // process). Owned by the view model, which disposes it on shutdown.
            var fileLogSink = FileLogSink.CreateDefault();
            return new MainWindow(
                new MainWindowViewModel(
                    projectSnapshot,
                    engine.OpenEditorSession(
                        projectDirectory.FullPath,
                        startRuntime: true,
                        logReceived: editorLog.AppendLine),
                    editorLog,
                    dispatch: action => Dispatcher.UIThread.Post(action),
                    projectDirectory: projectDirectory.FullPath,
                    fileLogSink: fileLogSink));
        }

        var project = new EngineProjectResponse
        {
            Ok = projectSnapshot.Ok,
            Status = projectSnapshot.Status,
            Error = projectSnapshot.Error,
        };
        var bootstrapWindow = new ProjectBootstrapWindow();
        bootstrapWindow.DataContext = new ProjectBootstrapViewModel(
            projectDirectory,
            engine,
            project,
            openProject: openedDirectory =>
            {
                var projectWindow = CreateProjectWindow(
                    desktop,
                    openedDirectory,
                    engine);

                desktop.MainWindow = projectWindow;
                projectWindow.Show();
                bootstrapWindow.Close();
            },
            quit: () => desktop.Shutdown());

        return bootstrapWindow;
    }
}
