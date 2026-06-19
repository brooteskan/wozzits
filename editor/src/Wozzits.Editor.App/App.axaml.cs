using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Avalonia.Platform.Storage;
using Wozzits.Editor.Core.Projects;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.App.Views;
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
        var editorHost = new WozzitsEditorHostClient();
        var launchOptions = ProjectLaunchOptions.FromCommandLine(desktop.Args ?? []);

        if (launchOptions.ProjectDirectory is null)
        {
            return CreateProjectDirectoryPickerWindow(desktop);
        }

        var projectDirectory = new ProjectDirectory(launchOptions.ProjectDirectory);
        return CreateProjectWindow(desktop, projectDirectory, editorHost);
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
            var editorHost = new WozzitsEditorHostClient();
            var projectWindow = CreateProjectWindow(desktop, projectDirectory, editorHost);

            desktop.MainWindow = projectWindow;
            projectWindow.Show();
            pickerWindow.Close();
        };

        return pickerWindow;
    }

    private static Window CreateProjectWindow(
        IClassicDesktopStyleApplicationLifetime desktop,
        ProjectDirectory projectDirectory,
        WozzitsEditorHostClient editorHost)
    {
        var project = editorHost.ProbeProject(projectDirectory.FullPath);

        if (project.IsValid)
        {
            return new MainWindow
            {
                DataContext = new MainWindowViewModel(
                    projectDirectory,
                    project.Project,
                    new WozzitsEditorHostSession(
                        editorHost.HostExecutablePath)),
            };
        }

        var bootstrapWindow = new ProjectBootstrapWindow();
        bootstrapWindow.DataContext = new ProjectBootstrapViewModel(
            projectDirectory,
            editorHost,
            project,
            openProject: openedDirectory =>
            {
                var projectWindow = CreateProjectWindow(
                    desktop,
                    openedDirectory,
                    editorHost);

                desktop.MainWindow = projectWindow;
                projectWindow.Show();
                bootstrapWindow.Close();
            },
            quit: () => desktop.Shutdown());

        return bootstrapWindow;
    }
}
