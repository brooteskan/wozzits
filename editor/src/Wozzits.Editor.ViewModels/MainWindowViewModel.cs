using System.Threading;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using Dock.Model.Core;
using Wozzits.Editor.Core.Logging;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.ViewModels;

public sealed partial class MainWindowViewModel : ViewModelBase
{
    private readonly SynchronizationContext? _syncContext = SynchronizationContext.Current;
    private readonly IDisposable? _editorSessionLifetime;
    private readonly IDisposable? _editorLogSubscription;
    private readonly IWozzitsEngineEditorSession? _editorSession;
    private readonly Action<Action>? _dispatch;
    private bool _shutdown;

    public MainWindowViewModel()
        : this(projectSnapshot: null)
    {
    }

    public MainWindowViewModel(
        EngineProjectSnapshotResponse? projectSnapshot = null,
        IWozzitsEngineEditorSession? editorSession = null,
        EditorLogBuffer? editorLog = null,
        Action<Action>? dispatch = null)
    {
        _editorSession = editorSession;
        _editorSessionLifetime = editorSession as IDisposable;
        _dispatch = dispatch;
        SaveAllCommand = new RelayCommand(SaveAll);
        RestartViewportCommand = new RelayCommand(RestartViewport, () => _editorSession is not null);
        AssetGraph = new AssetGraphEditorPaneViewModel(editorSession);
        AssetBrowser = new AssetBrowserPaneViewModel(editorSession);
        Inspector = new InspectorPaneViewModel(editorSession);
        SceneTree = new SceneTreeEditorPaneViewModel(editorSession);
        InitializeDockLayout();
        _editorLogSubscription = editorLog?.Subscribe(AppendEditorLog);

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
    }

    public string WindowTitle { get; } = "Wozzits";
    public string ProjectName { get; } = string.Empty;
    public AssetGraphEditorPaneViewModel AssetGraph { get; }
    public AssetBrowserPaneViewModel AssetBrowser { get; }
    public SceneTreeEditorPaneViewModel SceneTree { get; }
    public InspectorPaneViewModel Inspector { get; }
    public ConsolePaneViewModel Console { get; private set; } = null!;
    public IFactory DockFactory { get; private set; } = null!;
    public IRootDock EditorLayout { get; private set; } = null!;
    public IRelayCommand SaveAllCommand { get; }
    public IRelayCommand RestartViewportCommand { get; }

    public string EngineLogText => Console.LogText;

    public void Shutdown()
    {
        if (_shutdown)
        {
            return;
        }

        _shutdown = true;
        _editorSessionLifetime?.Dispose();
        _editorLogSubscription?.Dispose();
    }

    private void SaveAll()
    {
        _editorSession?.SaveAssetGraph();
        _editorSession?.SaveScene();
    }

    // Reopen the in-process engine viewport. Stops the current runtime if one is
    // still alive (or frees a closed/zombie one) and starts a fresh viewport for
    // the project - the way back after the viewport window has been closed.
    private void RestartViewport()
    {
        _editorSession?.RestartRuntime();
    }

    private void AppendEditorLog(string line)
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
