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
    private readonly IDisposable? _editorSessionLifetime;
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
        _editorSessionLifetime = editorSession as IDisposable;
        SaveAllCommand = new RelayCommand(SaveAll);
        AssetGraph = new AssetGraphEditorPaneViewModel(editorSession);
        Inspector = new InspectorPaneViewModel(editorSession);
        InitializeDockLayout();

        ProjectName = projectSnapshot?.ProjectName ?? string.Empty;
        WindowTitle = string.IsNullOrWhiteSpace(ProjectName)
            ? "Wozzits"
            : ProjectName;
        var projectAssetGraph = projectSnapshot?.AssetGraph;
        var sessionAssetGraph = editorSession?.LoadAssetGraphSnapshot();
        AssetGraph.LoadSnapshot(ChooseAssetGraphSnapshot(
            projectAssetGraph,
            sessionAssetGraph));
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
        _editorSessionLifetime?.Dispose();
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

    private static EngineAssetGraphSnapshotResponse? ChooseAssetGraphSnapshot(
        EngineAssetGraphSnapshotResponse? projectAssetGraph,
        EngineAssetGraphSnapshotResponse? sessionAssetGraph)
    {
        if (sessionAssetGraph?.Ok == true)
        {
            var projectNodeCount =
                projectAssetGraph?.Snapshot.Nodes.Count ?? 0;
            if (sessionAssetGraph.Snapshot.Nodes.Count > 0 || projectNodeCount == 0)
            {
                return sessionAssetGraph;
            }
        }

        return projectAssetGraph ?? sessionAssetGraph;
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
