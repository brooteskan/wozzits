using System.IO;
using Wozzits.Editor.Core.Projects;
using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.ViewModels;

public sealed partial class MainWindowViewModel : ViewModelBase
{
    public MainWindowViewModel()
        : this(ProjectLaunchOptions.FromCommandLine([]))
    {
    }

    public MainWindowViewModel(ProjectLaunchOptions launchOptions)
    {
        ArgumentNullException.ThrowIfNull(launchOptions);

        if (launchOptions.ProjectDirectory is null)
        {
            ProjectStatus = "No project directory supplied.";
            ProjectDirectory = "Run Wozzits Editor with a project directory path.";
            return;
        }

        var projectDirectory = new ProjectDirectory(launchOptions.ProjectDirectory);
        ProjectDirectory = projectDirectory.FullPath;
        ProjectStatus = Directory.Exists(projectDirectory.FullPath)
            ? "Project directory opened."
            : "Project directory does not exist.";
    }

    public MainWindowViewModel(ProjectDirectory projectDirectory, EngineProjectManifest? project = null)
    {
        ArgumentNullException.ThrowIfNull(projectDirectory);

        ProjectDirectory = projectDirectory.FullPath;
        ProjectStatus = "Project opened.";
        Project = project;
    }

    public string Title { get; } = "Wozzits Editor";

    public string ProjectStatus { get; }

    public string ProjectDirectory { get; }
    public EngineProjectManifest? Project { get; }
}
