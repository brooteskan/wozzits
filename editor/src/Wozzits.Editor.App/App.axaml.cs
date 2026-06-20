using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
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
        var projectSnapshot = engine.LoadProjectSnapshot(projectDirectory.FullPath);

        if (projectSnapshot.IsValid)
        {
            var editorHost = new WozzitsEditorHostClient();
            return new MainWindow(
                new MainWindowViewModel(
                    projectSnapshot,
                    engine.OpenEditorSession(
                        projectDirectory.FullPath,
                        startRuntime: true),
                    createHostSession: () => new WozzitsEditorHostSession(
                        editorHost.HostExecutablePath,
                        projectDirectory.FullPath),
                    dispatch: action => Dispatcher.UIThread.Post(action)));
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
