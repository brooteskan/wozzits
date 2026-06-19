using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.Core.Projects;

namespace Wozzits.Editor.ViewModels;

public sealed class ProjectBootstrapViewModel : ViewModelBase
{
    private readonly ProjectDirectory _projectDirectory;
    private readonly ProjectFileSet _projectFiles;
    private readonly Action<ProjectDirectory> _openProject;
    private string _errorMessage = string.Empty;

    public ProjectBootstrapViewModel(ProjectDirectory projectDirectory, Action<ProjectDirectory> openProject, Action quit)
    {
        _projectDirectory = projectDirectory ?? throw new ArgumentNullException(nameof(projectDirectory));
        _projectFiles = new ProjectFileSet(_projectDirectory);
        _openProject = openProject ?? throw new ArgumentNullException(nameof(openProject));

        ProjectDirectory = _projectDirectory.FullPath;
        ProjectStatus = Directory.Exists(_projectDirectory.FullPath)
            ? "No Wozzits project files found."
            : "The project directory will be created.";

        CreateProjectFilesCommand = new RelayCommand(CreateProjectFiles);
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

    public IRelayCommand CreateProjectFilesCommand { get; }

    public IRelayCommand QuitCommand { get; }

    private void CreateProjectFiles()
    {
        try
        {
            ErrorMessage = string.Empty;
            _projectFiles.Create();
            _openProject(_projectDirectory);
        }
        catch (Exception ex)
        {
            ErrorMessage = ex.Message;
        }
    }
}
