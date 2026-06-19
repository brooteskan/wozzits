using System.IO;
using System.Threading;
using Wozzits.Editor.Core.Projects;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.ViewModels;

public sealed partial class MainWindowViewModel : ViewModelBase
{
    private const int MaxEngineLogLines = 500;

    private readonly List<string> _engineLogLines = [];
    private readonly SynchronizationContext? _syncContext = SynchronizationContext.Current;
    private readonly WozzitsEditorHostSession? _editorHostSession;
    private bool _shutdown;

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

    public MainWindowViewModel(
        ProjectDirectory projectDirectory,
        EngineProjectManifest? project = null,
        WozzitsEditorHostSession? editorHostSession = null)
    {
        ArgumentNullException.ThrowIfNull(projectDirectory);

        ProjectDirectory = projectDirectory.FullPath;
        ProjectStatus = "Project opened.";
        Project = project;

        _editorHostSession = editorHostSession;
        if (_editorHostSession is not null)
        {
            _editorHostSession.LogReceived += AppendEngineLog;
            _editorHostSession.Start(projectDirectory.FullPath);
        }
    }

    public string Title { get; } = "Wozzits Editor";

    public string ProjectStatus { get; }

    public string ProjectDirectory { get; }
    public EngineProjectManifest? Project { get; }

    public string EngineLogText =>
        _engineLogLines.Count == 0
            ? "Editor host has not started."
            : string.Join(Environment.NewLine, _engineLogLines);

    public void Shutdown()
    {
        if (_shutdown)
        {
            return;
        }

        _shutdown = true;
        _editorHostSession?.Dispose();
    }

    private void AppendEngineLog(string line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return;
        }

        if (_syncContext is null || SynchronizationContext.Current == _syncContext)
        {
            AddEngineLogLine(line);
            return;
        }

        _syncContext.Post(_ => AddEngineLogLine(line), null);
    }

    private void AddEngineLogLine(string line)
    {
        _engineLogLines.Add(line);
        while (_engineLogLines.Count > MaxEngineLogLines)
        {
            _engineLogLines.RemoveAt(0);
        }

        OnPropertyChanged(nameof(EngineLogText));
    }
}
