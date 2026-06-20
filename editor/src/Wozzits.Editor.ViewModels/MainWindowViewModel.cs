using System.Threading;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using Dock.Model.Core;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.ViewModels;

public sealed partial class MainWindowViewModel : ViewModelBase
{
    private readonly SynchronizationContext? _syncContext = SynchronizationContext.Current;
    private readonly WozzitsEditorHostSession? _editorHostSession;
    private readonly Action<Action>? _dispatch;
    private bool _shutdown;

    public MainWindowViewModel()
        : this(projectSnapshot: null)
    {
    }

    public MainWindowViewModel(
        EngineProjectSnapshotResponse? projectSnapshot = null,
        WozzitsEditorHostSession? editorHostSession = null,
        IWozzitsEngineEditorSession? editorSession = null,
        Action<Action>? dispatch = null)
    {
        _dispatch = dispatch;
        SaveAllCommand = new RelayCommand(SaveAll);
        AssetGraph = new AssetGraphEditorPaneViewModel(editorSession);
        Inspector = new InspectorPaneViewModel(editorSession);
        InitializeDockLayout();

        ProjectName = projectSnapshot?.ProjectName ?? string.Empty;
        WindowTitle = string.IsNullOrWhiteSpace(ProjectName)
            ? "Wozzits"
            : ProjectName;
        AssetGraph.LoadSnapshot(projectSnapshot?.AssetGraph);
        SceneTree.LoadSnapshot(projectSnapshot?.Scene);

        _editorHostSession = editorHostSession;
        if (_editorHostSession is not null)
        {
            _editorHostSession.LogReceived += AppendEngineLog;
            _editorHostSession.Start();
        }
    }

    public string WindowTitle { get; } = "Wozzits";
    public string ProjectName { get; } = string.Empty;
    public AssetGraphEditorPaneViewModel AssetGraph { get; }
    public SceneTreeEditorPaneViewModel SceneTree { get; } = new();
    public InspectorPaneViewModel Inspector { get; }
    public ConsolePaneViewModel Console { get; private set; } = null!;
    public IFactory DockFactory { get; private set; } = null!;
    public IRootDock EditorLayout { get; private set; } = null!;
    public IRelayCommand SaveAllCommand { get; }

    public string EngineLogText => Console.LogText;

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

        if (_dispatch is not null)
        {
            _dispatch(() => AddEngineLogLine(line));
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
        Console.AppendLogLine(line);
        OnPropertyChanged(nameof(EngineLogText));
    }

    private static void SaveAll()
    {
    }

    private void InitializeDockLayout()
    {
        Console = new ConsolePaneViewModel();
        SceneTree.SelectedNodeChanged += Inspector.Inspect;
        AssetGraph.SelectedNodeChanged += Inspector.Inspect;

        var layoutFactory = new EditorDockLayoutFactory(this);
        DockFactory = layoutFactory.Factory;
        EditorLayout = layoutFactory.CreateLayout();
    }

}
