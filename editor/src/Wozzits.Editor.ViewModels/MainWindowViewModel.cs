using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using Dock.Model.Core;
using Wozzits.Editor.Core.Behaviors;
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
    private readonly string _projectDirectory;
    private readonly BehaviorModuleBuilder _behaviorBuilder = new();
    private bool _shutdown;

    public MainWindowViewModel()
        : this(projectSnapshot: null)
    {
    }

    public MainWindowViewModel(
        EngineProjectSnapshotResponse? projectSnapshot = null,
        IWozzitsEngineEditorSession? editorSession = null,
        EditorLogBuffer? editorLog = null,
        Action<Action>? dispatch = null,
        string? projectDirectory = null)
    {
        _editorSession = editorSession;
        _editorSessionLifetime = editorSession as IDisposable;
        _dispatch = dispatch;
        _projectDirectory = projectDirectory ?? string.Empty;
        SaveAllCommand = new RelayCommand(SaveAll);
        RestartViewportCommand = new RelayCommand(RestartViewport, () => _editorSession is not null);
        RebuildBehaviorsCommand = new AsyncRelayCommand(
            RebuildBehaviorsAsync,
            () => _editorSession is not null);
        AssetGraph = new AssetGraphEditorPaneViewModel(editorSession);
        AssetBrowser = new AssetBrowserPaneViewModel(editorSession);
        Inspector = new InspectorPaneViewModel(
            editorSession, AppendEditorLog, _projectDirectory);
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

        // Merge the runtime's grafted "Subtree from asset" children under their
        // hosts (issue #213). Deferred off the constructor: the query blocks on the
        // engine thread until the runtime has loaded + grafted (seconds during a
        // cold start), so posting it lets the window paint first. Falls back to a
        // direct call when there is no dispatcher (design-time / tests).
        if (_dispatch is not null)
        {
            _dispatch(() => SceneTree.MergeGraftedNodes());
        }
        else
        {
            SceneTree.MergeGraftedNodes();
        }
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
    public IAsyncRelayCommand RebuildBehaviorsCommand { get; }

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

    // Recompile the project's behavior-module DLLs (cmake, streamed to the
    // console) and, on success, hot-reload them into the running engine without
    // restarting the viewport. Mirrors the imgui toolhost editor's Rebuild step.
    // The command disables itself while running (AsyncRelayCommand default).
    private async Task RebuildBehaviorsAsync()
    {
        if (_editorSession is null)
        {
            return;
        }

        AppendEditorLog("[editor] Rebuilding behavior modules...");

        BehaviorBuildOutcome outcome;
        try
        {
            outcome = await _behaviorBuilder.RebuildAsync(
                _projectDirectory,
                AppendEditorLog);
        }
        catch (Exception ex)
        {
            AppendEditorLog($"[editor] Behavior rebuild error: {ex.Message}");
            return;
        }

        switch (outcome)
        {
            case BehaviorBuildOutcome.Failed:
                AppendEditorLog(
                    "[editor] Behavior rebuild failed; modules not reloaded.");
                return;
            case BehaviorBuildOutcome.Skipped:
                // Nothing was built, so there is nothing to hot-reload.
                return;
        }

        var reload = _editorSession.ReloadBehaviorModules();
        AppendEditorLog(reload.Ok
            ? "[editor] Behavior modules reloaded."
            : $"[editor] Behavior reload skipped: {reload.Error}");
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
        // The scene tree and asset graph share one inspector and must show a
        // single active selection: selecting in one pane clears the other (and
        // its highlight) so the inspector tracks the highlighted node; an empty
        // selection shows no inspector.
        SceneTree.SelectedNodeChanged += OnSceneNodeSelected;
        AssetGraph.SelectedNodeChanged += OnAssetGraphNodeSelected;
        // A "Subtree from asset" assign/clear changes the runtime's grafted
        // children (issue #213); re-merge them into the scene tree under the host.
        Inspector.SceneSourceChanged += OnInspectorSceneSourceChanged;

        var layoutFactory = new EditorDockLayoutFactory(this);
        DockFactory = layoutFactory.Factory;
        EditorLayout = layoutFactory.CreateLayout();
    }

    // Guards against re-entrancy: clearing one pane raises its
    // SelectedNodeChanged(null), which must not recurse back through here.
    private bool _syncingSelection;

    private void OnSceneNodeSelected(SceneTreeNodeViewModel? node)
    {
        if (_syncingSelection)
        {
            return;
        }
        if (node is not null)
        {
            _syncingSelection = true;
            try
            {
                AssetGraph.ClearSelection();
            }
            finally
            {
                _syncingSelection = false;
            }
        }
        RefreshInspectorSceneSources();
        Inspector.Inspect(node);
    }

    private void OnAssetGraphNodeSelected(AssetGraphNodeCardViewModel? node)
    {
        if (_syncingSelection)
        {
            return;
        }
        if (node is not null)
        {
            _syncingSelection = true;
            try
            {
                SceneTree.ClearSelection();
            }
            finally
            {
                _syncingSelection = false;
            }
        }
        RefreshInspectorSceneSources();
        Inspector.Inspect(node);
    }

    // Thread the asset graph's "Scene from GLB" nodes into the inspector's "Subtree
    // from asset" picker (issue #213 piece 2). Refreshed on each selection from the
    // loaded snapshot — a snapshot-time list is sufficient for piece 2; it does not
    // track live graph edits. The inspector takes plain option data, so it never
    // depends on the asset-graph pane.
    private void RefreshInspectorSceneSources()
    {
        Inspector.SetAvailableSceneSources(
            AssetGraph.Nodes
                .Where(node => string.Equals(
                    node.TypeName,
                    SceneFromGlbTypeName,
                    System.StringComparison.Ordinal))
                .Select(node => new InspectorSceneSourceOptionViewModel(
                    node.Id,
                    node.DisplayName)));
    }

    // A scene-source reference/descriptor was assigned or cleared in the inspector
    // (issue #213): the runtime re-grafted, so re-merge its grafted children into
    // the scene tree under their hosts. The merge re-queries the runtime and
    // de-dupes its own previous grafts, so calling it after every change is safe.
    private void OnInspectorSceneSourceChanged()
    {
        SceneTree.MergeGraftedNodes();
    }

    // The asset-graph node type that produces a graftable scene hierarchy (issue
    // #213); registered in the engine's type_extensions under this name.
    private const string SceneFromGlbTypeName = "Scene from GLB";
}
