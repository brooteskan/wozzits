using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.Core.Projects;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.ViewModels;

public sealed class ProjectBootstrapViewModel : ViewModelBase
{
    private readonly ProjectDirectory _projectDirectory;
    private readonly EngineProjectHostClient _projectHost;
    private readonly Action<ProjectDirectory> _openProject;
    private string _errorMessage = string.Empty;

    public ProjectBootstrapViewModel(
        ProjectDirectory projectDirectory,
        EngineProjectHostClient projectHost,
        EngineProjectResponse projectStatus,
        Action<ProjectDirectory> openProject,
        Action quit)
    {
        _projectDirectory = projectDirectory ?? throw new ArgumentNullException(nameof(projectDirectory));
        _projectHost = projectHost ?? throw new ArgumentNullException(nameof(projectHost));
        ArgumentNullException.ThrowIfNull(projectStatus);
        _openProject = openProject ?? throw new ArgumentNullException(nameof(openProject));

        ProjectDirectory = _projectDirectory.FullPath;
        ProjectStatus = projectStatus.IsMissing
            ? Directory.Exists(_projectDirectory.FullPath)
                ? "No Wozzits project files found."
                : "The project directory will be created."
            : "Wozzits project is invalid.";
        ErrorMessage = projectStatus.IsMissing ? string.Empty : projectStatus.Error;
        CanCreateProject = projectStatus.IsMissing;

        CreateProjectFilesCommand = new RelayCommand(CreateProjectFiles, () => CanCreateProject);
        QuitCommand = new RelayCommand(quit ?? throw new ArgumentNullException(nameof(quit)));
    }

    public string Title { get; } = "Create Wozzits Project";

    public string ProjectStatus { get; }

    public string ProjectDirectory { get; }

    public string ErrorMessage
    {
        get => _errorMessage;
        private set => SetProperty(ref _errorMessage, value);
    }

    public bool CanCreateProject { get; }

    public IRelayCommand CreateProjectFilesCommand { get; }

    public IRelayCommand QuitCommand { get; }

    private void CreateProjectFiles()
    {
        try
        {
            ErrorMessage = string.Empty;
            var created = _projectHost.CreateProject(_projectDirectory.FullPath);
            if (!created.Ok)
            {
                ErrorMessage = created.Error;
                return;
            }
            _openProject(_projectDirectory);
        }
        catch (Exception ex)
        {
            ErrorMessage = ex.Message;
        }
    }
}
