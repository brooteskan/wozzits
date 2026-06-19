using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using Wozzits.Editor.Core.Projects;
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

    private static Avalonia.Controls.Window CreateStartupWindow(IClassicDesktopStyleApplicationLifetime desktop)
    {
        var launchOptions = ProjectLaunchOptions.FromCommandLine(desktop.Args ?? []);

        if (launchOptions.ProjectDirectory is null)
        {
            return new MainWindow
            {
                DataContext = new MainWindowViewModel(launchOptions),
            };
        }

        var projectDirectory = new ProjectDirectory(launchOptions.ProjectDirectory);
        var projectFiles = new ProjectFileSet(projectDirectory);

        if (projectFiles.Exists())
        {
            return new MainWindow
            {
                DataContext = new MainWindowViewModel(projectDirectory),
            };
        }

        var bootstrapWindow = new ProjectBootstrapWindow();
        bootstrapWindow.DataContext = new ProjectBootstrapViewModel(
            projectDirectory,
            openProject: openedDirectory =>
            {
                var mainWindow = new MainWindow
                {
                    DataContext = new MainWindowViewModel(openedDirectory),
                };

                desktop.MainWindow = mainWindow;
                mainWindow.Show();
                bootstrapWindow.Close();
            },
            quit: () => desktop.Shutdown());

        return bootstrapWindow;
    }
}
