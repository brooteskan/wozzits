using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.Input;
using Dock.Model.Controls;
using Dock.Model.Core;
using Dock.Model.Mvvm.Controls;
using Wozzits.Editor.Core.Behaviors;
using Wozzits.Editor.Core.Logging;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels.EditorPanes;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;
using Wozzits.Editor.Statecharts;

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
    // One grouping model shared by the root canvas and every drill-in tab, so sub-graphs
    // and their membership stay consistent across panes (issue woguls/wozzits-editor#1).
    private readonly AssetGraphGroupingModel _subGraphGrouping = new();
    private readonly AssetGraphRerouteModel _subGraphReroutes = new();
    private IDocumentDock? _assetGraphDock;

    // Engine log lines arrive one at a time (often on the engine's logger thread)
    // and used to each trigger a full console rebuild + re-render on the UI thread
    // — a resolve's burst of hundreds froze the editor. Buffer them and drain on a
    // single coalesced UI-thread flush so a burst is one update, not one per line.
    private readonly object _pendingLogGate = new();
    private readonly List<string> _pendingLogLines = [];
    private bool _logFlushScheduled;

    // Mirror every console line to a per-run, uncapped, timestamped file (the UI
    // console keeps only the last N lines). Captures engine + editor + the separate
    // play process, all of which converge on AppendEditorLog.
    private readonly FileLogSink? _fileLogSink;
    private readonly StandaloneAppLauncher _standaloneLauncher = new();
    private Process? _standaloneProcess;
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
        string? projectDirectory = null,
        FileLogSink? fileLogSink = null)
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
        LaunchStandaloneCommand = new RelayCommand(
            LaunchStandalone,
            () => !string.IsNullOrWhiteSpace(_projectDirectory));
        OpenSceneletCommand = new RelayCommand<SceneletInfo?>(
            OpenScenelet, _ => _editorSession is not null);
        BackToSceneCommand = new RelayCommand(
            BackToScene, () => _editorSession is not null && IsEditingPrefab);
        RefreshSceneletsCommand = new RelayCommand(
            RefreshScenelets, () => _editorSession is not null);
        RefreshStatechartsCommand = new RelayCommand(
            RefreshStatecharts, () => !string.IsNullOrWhiteSpace(_projectDirectory));
        NewStatechartCommand = new RelayCommand(
            NewStatechart, () => !string.IsNullOrWhiteSpace(_projectDirectory) && _assetGraphDock is not null);
        OpenStatechartCommand = new RelayCommand<StatechartFileInfo?>(
            OpenStatechart, _ => _assetGraphDock is not null);
        AssetGraph = new AssetGraphEditorPaneViewModel(
            editorSession,
            _subGraphGrouping,
            reroutes: _subGraphReroutes);
        AssetBrowser = new AssetBrowserPaneViewModel(editorSession);
        Inspector = new InspectorPaneViewModel(
            editorSession, AppendEditorLog);
        SceneTree = new SceneTreeEditorPaneViewModel(
            editorSession, _projectDirectory, AppendEditorLog);
        // The renderable-ingredients form (issue #230) resolves the node's
        // EFFECTIVE render program by walking ParentId ancestors, which needs
        // the scene tree's node lookup (the inspector holds only the selection).
        Inspector.SetSceneNodeLookup(SceneTree.FindNodeById);
        Inspector.SetRerouteModel(_subGraphReroutes);
        InitializeDockLayout();

        // The per-run file mirror is composed at the app root (null in tests, so a
        // test never writes a log file). Announce its path BEFORE subscribing so the
        // console points at the full, uncapped log and the buffer's replayed preamble
        // ("Opening project", "abi vN", "Project loaded") lands in the file too.
        _fileLogSink = fileLogSink;
        if (_fileLogSink?.FilePath is { } logPath)
        {
            AppendEditorLog($"[editor] Session log: {logPath}");
        }

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
        LoadSubGraphSidecar();
        SceneTree.LoadSnapshot(projectSnapshot?.Scene);

        // Merge the runtime's grafted "Subtree from asset" children under their
        // hosts (issue #213). Deferred off the constructor: the query blocks on the
        // engine thread until the runtime has loaded + grafted (seconds during a
        // cold start), so posting it lets the window paint first. Falls back to a
        // direct call when there is no dispatcher (design-time / tests).
        // Merge grafted nodes, then populate the scenelet (prefab) menu -- in ONE
        // deferred step so the menu refresh runs AFTER the graft merge (which blocks
        // on the engine thread until the viewport has loaded + published its
        // scenelet catalog). A separate post could race the still-loading runtime
        // and read an empty catalog.
        void InitializeAfterViewportReady()
        {
            SceneTree.MergeGraftedNodes();
            RefreshScenelets();
        }

        if (_dispatch is not null)
        {
            _dispatch(InitializeAfterViewportReady);
        }
        else
        {
            InitializeAfterViewportReady();
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
    public IRelayCommand LaunchStandaloneCommand { get; }

    // Prefab (scenelet) editing: the project's scenelets for the menu, the
    // currently-open prefab (null => editing the main scene), and the commands to
    // open a prefab in the viewport / switch back.
    public ObservableCollection<SceneletInfo> Scenelets { get; } = new();
    public IRelayCommand<SceneletInfo?> OpenSceneletCommand { get; }
    public IRelayCommand BackToSceneCommand { get; }
    public IRelayCommand RefreshSceneletsCommand { get; }

    public IRelayCommand RefreshStatechartsCommand { get; }

    public IRelayCommand NewStatechartCommand { get; }

    public IRelayCommand<StatechartFileInfo?> OpenStatechartCommand { get; }

    public ObservableCollection<StatechartFileInfo> Statecharts { get; } = [];

    private string? _editingPrefabName;
    public string? EditingPrefabName
    {
        get => _editingPrefabName;
        private set
        {
            if (SetProperty(ref _editingPrefabName, value))
            {
                OnPropertyChanged(nameof(IsEditingPrefab));
                BackToSceneCommand.NotifyCanExecuteChanged();
            }
        }
    }

    public bool IsEditingPrefab => !string.IsNullOrEmpty(_editingPrefabName);

    // Frame profiling is opt-in (default off). Toggling drives the running engine
    // through the runtime seam: OFF records nothing and writes no CSV; ON starts a
    // fresh capture that flushes to its own frame_profile_<tag>.csv on OFF or on
    // app close. A no-op when no viewport is running.
    private bool _frameProfilingEnabled;
    public bool FrameProfilingEnabled
    {
        get => _frameProfilingEnabled;
        set
        {
            if (SetProperty(ref _frameProfilingEnabled, value))
            {
                _editorSession?.SetFrameProfiling(value);
            }
        }
    }

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
        _fileLogSink?.Dispose();
    }

    private void SaveAll()
    {
        _editorSession?.SaveAssetGraph();
        _editorSession?.SaveScene();
        SaveSubGraphSidecar();
        SaveOpenStatecharts();
    }

    // Editor-only sub-graph groupings persist in a sidecar next to the asset graph
    // (issue woguls/wozzits-editor#1), owned entirely by the editor — the engine and asset
    // compiler never read it. Written on Save All, read at project open, keyed by stable
    // node id so it survives graph reloads/rebuilds. Skipped without a project directory
    // (design-time / tests).
    private void SaveSubGraphSidecar()
    {
        if (SubGraphSidecarPath() is not { } path)
        {
            return;
        }

        try
        {
            File.WriteAllText(
                path,
                AssetGraphSubGraphSidecar.Serialize(
                    AssetGraph.SubGraphs,
                    AssetGraph.RerouteNames));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            AppendEditorLog($"[editor] Sub-graph layout save failed: {ex.Message}");
        }
    }

    // Save every open statechart document that has edits: the chart back to its .sc.json (if
    // the model changed) and its hand-placed layout to a .sc.editor.json sidecar. The editor
    // owns both files; the engine reads neither directly.
    private void SaveOpenStatecharts()
    {
        if (_assetGraphDock?.VisibleDockables is null)
        {
            return;
        }

        foreach (var dockable in _assetGraphDock.VisibleDockables)
        {
            if (dockable is Document { Context: StatechartDocumentViewModel document } && document.IsDirty)
            {
                try
                {
                    document.Save();
                    AppendEditorLog($"[editor] Saved statechart '{document.Name}'");
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
                {
                    AppendEditorLog($"[editor] Statechart save failed for '{document.Name}': {ex.Message}");
                }
            }
        }
    }

    private void LoadSubGraphSidecar()
    {
        if (SubGraphSidecarPath() is not { } path || !File.Exists(path))
        {
            return;
        }

        try
        {
            var document = AssetGraphSubGraphSidecar.Deserialize(File.ReadAllText(path));
            AssetGraph.LoadSubGraphs(document.SubGraphs);
            AssetGraph.LoadReroutes(document.Reroutes);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            AppendEditorLog($"[editor] Sub-graph layout load failed: {ex.Message}");
        }
    }

    private string? SubGraphSidecarPath()
    {
        return string.IsNullOrWhiteSpace(_projectDirectory)
            ? null
            : Path.Combine(_projectDirectory, AssetGraphSubGraphSidecar.FileName);
    }

    // Prefab (scenelet) editing (same-window round-trip): open a scenelet as the
    // viewport's working scene, edit it with the normal tools, SaveAll persists it,
    // and "Back to Scene" returns to the main scene.
    private void RefreshScenelets()
    {
        if (_editorSession is null)
        {
            AppendEditorLog("[editor] Prefab list: no engine session.");
            return;
        }
        Scenelets.Clear();
        foreach (var scenelet in _editorSession.GetSceneletCatalog())
        {
            Scenelets.Add(scenelet);
        }
        // Diagnostic: tells us whether the engine handed back scenelets (a non-zero
        // count with an empty menu = a XAML binding problem, not an ABI/engine one).
        AppendEditorLog(
            $"[editor] Prefab list: {Scenelets.Count} scenelet(s) "
            + $"[{string.Join(", ", Scenelets.Select(s => s.Name))}].");
    }

    private void OpenScenelet(SceneletInfo? scenelet)
    {
        if (_editorSession is null || scenelet is null)
        {
            return;
        }
        var result = _editorSession.OpenScene(scenelet.Path);
        if (!result.Ok)
        {
            AppendEditorLog(
                $"[editor] Open prefab '{scenelet.Name}' failed: {result.Error}");
            return;
        }
        EditingPrefabName = scenelet.Name;
        AppendEditorLog($"[editor] Editing prefab '{scenelet.Name}'.");
        ReloadSceneFromRuntime();
    }

    private void BackToScene()
    {
        if (_editorSession is null)
        {
            return;
        }
        // Empty path reopens the project's main scene (the engine tracks it).
        var result = _editorSession.OpenScene(string.Empty);
        if (!result.Ok)
        {
            AppendEditorLog($"[editor] Back to scene failed: {result.Error}");
            return;
        }
        EditingPrefabName = null;
        AppendEditorLog("[editor] Back to the main scene.");
        ReloadSceneFromRuntime();
    }

    // Rebuild the tree/inspector from the engine's LIVE scene after a swap (the
    // running scene is now the scenelet, or the main scene again).
    private void ReloadSceneFromRuntime()
    {
        if (_editorSession is null)
        {
            return;
        }
        SceneTree.LoadSnapshot(_editorSession.LoadRuntimeSceneSnapshot());
        SceneTree.MergeGraftedNodes();
    }

    // Reopen the in-process engine viewport. Stops the current runtime if one is
    // still alive (or frees a closed/zombie one) and starts a fresh viewport for
    // the project - the way back after the viewport window has been closed.
    private void RestartViewport()
    {
        _editorSession?.RestartRuntime();
        // The runtime is back (or gone): re-enable the inspector's edit surface.
        // The scene tree self-heals (it re-checks on context-menu open).
        Inspector.RefreshEditAvailability();
    }

    // Launch the project as a SEPARATE process (the shipped-app play path),
    // distinct from the in-process resident viewport. The standalone loads the
    // project's behavior-module DLLs, so behaviors play faithfully. The play
    // runs in its own window; closing the editor closes the host's stdin pipe,
    // which ends the play gracefully.
    private void LaunchStandalone()
    {
        _standaloneProcess = _standaloneLauncher.Launch(
            _projectDirectory,
            AppendEditorLog);
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

        // Mirror to the uncapped file first (its own lock; never nested with the
        // UI-batch gate below), so a line survives even if the UI flush never runs.
        _fileLogSink?.Write(line);

        bool scheduleFlush;
        lock (_pendingLogGate)
        {
            _pendingLogLines.Add(line);
            scheduleFlush = !_logFlushScheduled;
            _logFlushScheduled = true;
        }

        // Only one flush is ever in flight. Lines that arrive while it is queued
        // accumulate under the lock and drain together, so a burst collapses to a
        // single UI-thread update instead of one marshal + rebuild per line.
        if (!scheduleFlush)
        {
            return;
        }

        if (_dispatch is not null)
        {
            _dispatch(FlushPendingLogLines);
        }
        else if (_syncContext is null || SynchronizationContext.Current == _syncContext)
        {
            FlushPendingLogLines();
        }
        else
        {
            _syncContext.Post(_ => FlushPendingLogLines(), null);
        }
    }

    private void FlushPendingLogLines()
    {
        List<string> batch;
        lock (_pendingLogGate)
        {
            _logFlushScheduled = false;
            if (_pendingLogLines.Count == 0)
            {
                return;
            }

            batch = new List<string>(_pendingLogLines);
            _pendingLogLines.Clear();
        }

        Console.AppendLogLines(batch);
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
        // Applying a node param in the inspector updates the engine draft but not
        // the asset-graph pane's cached node cards; re-pull the graph so
        // re-selecting the node shows the applied value (#218 Phase 3).
        Inspector.AssetGraphNodeParamApplied += OnInspectorAssetGraphNodeParamApplied;
        Inspector.RerouteChanged += OnInspectorRerouteChanged;

        var layoutFactory = new EditorDockLayoutFactory(this);
        DockFactory = layoutFactory.Factory;
        EditorLayout = layoutFactory.CreateLayout();
        _assetGraphDock = layoutFactory.AssetGraphDock;
        AssetGraph.OpenSubGraphRequested += OnOpenSubGraphRequested;
        AssetGraph.SelectedSubGraphChanged += OnAssetGraphSubGraphSelected;
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

    // Selecting a sub-graph proxy inspects it (it is nameable there) and clears the
    // scene-tree selection, mirroring node selection's mutual exclusivity.
    private void OnAssetGraphSubGraphSelected(AssetGraphSubGraph? subGraph)
    {
        if (_syncingSelection)
        {
            return;
        }

        if (subGraph is not null)
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

        Inspector.Inspect(subGraph);
    }

    // Open (or re-focus) a sub-graph in its own document tab (drill-in). The tab is a
    // second asset-graph pane over the SAME engine session and the SAME shared grouping,
    // with its canvas context set to the sub-graph, so it shows just that group's members
    // and edits flow to the one draft (issue woguls/wozzits-editor#1). Closing the tab
    // drops the pane; reopening rebuilds it from the live graph.
    private void OnOpenSubGraphRequested(AssetGraphSubGraph subGraph)
    {
        if (_assetGraphDock is null || _editorSession is null)
        {
            return;
        }

        var documentId = $"SubGraph_{subGraph.Id}";
        var existing = _assetGraphDock.VisibleDockables?
            .FirstOrDefault(dockable => dockable.Id == documentId);
        if (existing is not null)
        {
            DockFactory.SetActiveDockable(existing);
            return;
        }

        var pane = new AssetGraphEditorPaneViewModel(
            _editorSession,
            _subGraphGrouping,
            subGraph,
            _subGraphReroutes);
        pane.LoadSnapshot(_editorSession.LoadAssetGraphSnapshot());
        pane.OpenSubGraphRequested += OnOpenSubGraphRequested;
        pane.SelectedNodeChanged += OnAssetGraphNodeSelected;
        pane.SelectedSubGraphChanged += OnAssetGraphSubGraphSelected;

        var document = new Document
        {
            Id = documentId,
            Title = subGraph.Name,
            Context = pane,
            CanClose = true,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = true,
            DockCapabilityOverrides = new DockCapabilityOverrides
            {
                CanClose = true,
                CanPin = false,
                CanFloat = false,
                CanDrag = true,
                CanDrop = true,
                CanDockAsDocument = true,
            },
        };

        DockFactory.AddDockable(_assetGraphDock, document);
        DockFactory.SetActiveDockable(document);
    }

    // Enumerate the project's authored statecharts (behavior/statecharts/*.sc.json) for
    // the open menu. Re-run when the menu opens so newly-authored charts appear.
    private void RefreshStatecharts()
    {
        Statecharts.Clear();
        if (string.IsNullOrWhiteSpace(_projectDirectory))
        {
            return;
        }

        var dir = Path.Combine(_projectDirectory, "behavior", "statecharts");
        if (!Directory.Exists(dir))
        {
            return;
        }

        foreach (var path in Directory.EnumerateFiles(dir, "*.sc.json").OrderBy(p => p))
        {
            var file = Path.GetFileName(path);
            var name = file.EndsWith(".sc.json", StringComparison.Ordinal) ? file[..^8] : file;
            Statecharts.Add(new StatechartFileInfo(name, path));
        }
    }

    // Create a fresh chart in the project's statecharts folder and open it. Seeded with one
    // region + state so it's valid and its toolbars show (you author the rest from there). The
    // Statecharts menu is the one place charts are created/opened.
    private void NewStatechart()
    {
        if (string.IsNullOrWhiteSpace(_projectDirectory) || _assetGraphDock is null)
        {
            return;
        }

        var dir = Path.Combine(_projectDirectory, "behavior", "statecharts");
        Directory.CreateDirectory(dir);
        var name = FreshStatechartName(dir);
        var path = Path.Combine(dir, name + ".sc.json");

        var chart = new Chart { Name = name };
        chart.States.Add(new State { Id = "state1" });
        var region = new Region { Id = "region1", Initial = "state1" };
        region.States.Add("state1");
        chart.Regions.Add(region);

        try
        {
            File.WriteAllText(path, StatechartJson.Emit(chart, indented: true));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            AppendEditorLog($"[editor] Could not create statechart '{name}': {ex.Message}");
            return;
        }

        RefreshStatecharts();
        OpenStatechart(new StatechartFileInfo(name, path));
        AppendEditorLog($"[editor] Created statechart '{name}'");
    }

    private static string FreshStatechartName(string dir)
    {
        for (int i = 1; ; i++)
        {
            var name = "untitled" + i;
            if (!File.Exists(Path.Combine(dir, name + ".sc.json")))
            {
                return name;
            }
        }
    }

    // Open a chart as ONE document hosting both canvases (control over dataflow, split).
    // Thick editor: the .sc.json is loaded + compiled in-process; the engine isn't involved.
    // Re-focuses an already-open tab rather than duplicating it.
    private void OpenStatechart(StatechartFileInfo? info)
    {
        if (info is null || _assetGraphDock is null)
        {
            return;
        }

        var documentId = $"Statechart_{info.Name}";
        var existing = _assetGraphDock.VisibleDockables?
            .FirstOrDefault(dockable => dockable.Id == documentId);
        if (existing is not null)
        {
            DockFactory.SetActiveDockable(existing);
            return;
        }

        Chart chart;
        try
        {
            chart = StatechartJson.Load(File.ReadAllText(info.Path));
        }
        catch (Exception ex)
        {
            AppendEditorLog($"[editor] Could not open statechart '{info.Name}': {ex.Message}");
            return;
        }

        var chartDocument = new StatechartDocumentViewModel(info.Name, info.Path, chart);
        chartDocument.Control.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(ControlPaneViewModel.SelectedState))
            {
                Inspector.Inspect(chartDocument.Control.SelectedState);
            }
        };
        chartDocument.Dataflow.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(DataflowPaneViewModel.SelectedNode))
            {
                Inspector.Inspect(chartDocument.Dataflow.SelectedNode);
            }
        };

        var document = new Document
        {
            Id = documentId,
            Title = info.Name,
            Context = chartDocument,
            CanClose = true,
            CanFloat = false,
            CanDrag = true,
            CanDrop = true,
            CanDockAsDocument = true,
            DockCapabilityOverrides = new DockCapabilityOverrides
            {
                CanClose = true,
                CanPin = false,
                CanFloat = false,
                CanDrag = true,
                CanDrop = true,
                CanDockAsDocument = true,
            },
        };

        chartDocument.Renamed = () =>
        {
            document.Title = chartDocument.Name;
            document.Id = $"Statechart_{chartDocument.Name}";
            RefreshStatecharts();
        };
        chartDocument.Deleted = () =>
        {
            DockFactory.CloseDockable(document);
            RefreshStatecharts();
        };

        DockFactory.AddDockable(_assetGraphDock, document);
        DockFactory.SetActiveDockable(document);
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
                .Where(IsSceneFromGlbNode)
                .Select(node => new InspectorSceneSourceOptionViewModel(
                    node.Id,
                    node.DisplayName)));

        // Thread the "Render program" candidates (issue #213) the same way: the
        // inspector takes plain option data.
        RefreshInspectorRenderProgramSources();

        // Thread the live asset-graph topology so the inspector's "GLB node" tree
        // picker (issue #213) can walk the selected "Mesh from GLB scene" node's
        // `scene` → `source_file` edges to the connected GLB file's source_path.
        // Built from the live pane VMs (kept current across graph edits), projected
        // back to the plain protocol shape the inspector consumes — only the fields
        // the traversal reads (node id, input ports, params; edge endpoints + port).
        RefreshInspectorAssetGraphTopology();
    }

    // Project the asset-graph pane's live node/edge VMs back to the minimal protocol
    // records the inspector's traversals need, and hand them over: the GLB-node
    // picker walks input ports + params, and the renderable-ingredients form
    // (issue #230) additionally reads OUTPUT port types to offer kind-filtered
    // binding sources and follows the program→layout edge to the layout params.
    private void RefreshInspectorAssetGraphTopology()
    {
        var nodes = AssetGraph.Nodes
            .Select(node => new EngineAssetGraphNode
            {
                Id = node.Id,
                Schema = node.SchemaLabel,
                DisplayName = node.DisplayName,
                InputPorts = node.InputPorts
                    .Select(port => new EngineAssetGraphPort
                    {
                        Index = port.Index,
                        Name = port.Name,
                    })
                    .ToList(),
                OutputPorts = node.OutputPorts
                    .Select(port => new EngineAssetGraphPort
                    {
                        Index = port.Index,
                        Type = port.Type,
                        Name = port.Name,
                    })
                    .ToList(),
                Params = node.Params.ToList(),
            })
            .ToList();

        var edges = AssetGraph.Edges
            .Select(edge => new EngineAssetGraphEdge
            {
                Id = edge.Id,
                From = edge.FromNodeId,
                To = edge.ToNodeId,
                ToInputPort = edge.ToInputPort,
            })
            .ToList();

        Inspector.SetAssetGraphTopology(nodes, edges);
    }

    // Thread the "Render program" picker (issue #213) with the asset-graph nodes
    // whose OUTPUT asset type is RenderProgram (1049). Filtering on the output
    // port's asset type (not schema label) covers every schema that yields the type
    // and matches what the engine routes on when assembling the renderable.
    // Refreshed per selection from the snapshot.
    private void RefreshInspectorRenderProgramSources()
    {
        Inspector.SetAvailableRenderPrograms(
            AssetGraph.Nodes
                .Where(IsRenderProgramNode)
                .Select(node => new InspectorAssetGraphRefOptionViewModel(
                    node.Id,
                    node.DisplayName)));

        // Thread the "Collision" picker (terrain-stick track) the same way: the
        // inspector takes plain option data, filtered to Collision outputs.
        RefreshInspectorCollisionSources();

        // Thread the "Audio Source" picker (audio-track item 10), filtered to
        // audio-renderable outputs.
        RefreshInspectorAudioRenderableSources();
    }

    // Thread the "Audio Source" picker with the asset-graph nodes whose OUTPUT
    // asset type is AudioRenderable (2142), filtering on the output port's asset
    // type exactly as the collision/render-program pickers do.
    private void RefreshInspectorAudioRenderableSources()
    {
        Inspector.SetAvailableAudioRenderables(
            AssetGraph.Nodes
                .Where(IsAudioRenderableNode)
                .Select(node => new InspectorAssetGraphRefOptionViewModel(
                    node.Id,
                    node.DisplayName)));
    }

    // True when a node produces an audio renderable the AudioSource component can
    // reference (kAssetTypeAudioRenderable = 2142 in type_extensions.h).
    private static bool IsAudioRenderableNode(AssetGraphNodeCardViewModel node) =>
        node.OutputPorts.Any(port => port.Type == AudioRenderableAssetTypeId);

    private const uint AudioRenderableAssetTypeId = 2142;

    // Thread the "Collision" picker (terrain-stick track) with the asset-graph
    // nodes whose OUTPUT asset type is Collision (150). Filtering on the output
    // port's asset type (not schema label) covers every schema that yields the type
    // ("Collision from mesh"/"Collision from terrain"), exactly as the render-
    // program picker does. Refreshed per selection from the snapshot.
    private void RefreshInspectorCollisionSources()
    {
        Inspector.SetAvailableCollisionSources(
            AssetGraph.Nodes
                .Where(IsCollisionNode)
                .Select(node => new InspectorAssetGraphRefOptionViewModel(
                    node.Id,
                    node.DisplayName)));
    }

    // True when a node produces a render program the render-program component can
    // consume (RenderProgram = 1049 in type_extensions.h, the value the engine's
    // assemble routes on).
    private static bool IsRenderProgramNode(AssetGraphNodeCardViewModel node) =>
        node.OutputPorts.Any(port => port.Type == RenderProgramAssetTypeId);

    private const uint RenderProgramAssetTypeId = 1049;

    // True when a node produces a Collision asset the Collision component can
    // reference (kAssetTypeCollisionAsset = 150 in type_extensions.h).
    private static bool IsCollisionNode(AssetGraphNodeCardViewModel node) =>
        node.OutputPorts.Any(port => port.Type == CollisionAssetTypeId);

    private const uint CollisionAssetTypeId = 150;

    // True for a "Scene from GLB" asset-graph node — the only graftable subtree
    // source the picker offers (issue #213 piece 2).
    //
    // Matching by node.TypeName is WRONG: TypeName is the asset *type* display name
    // ("Scene"), which a Scene-from-JSON node shares, so it never identifies the GLB
    // schema and would also wrongly include scene-from-JSON. The stable per-schema
    // discriminator is node.SchemaLabel (the engine's schema_tail — the low 32 bits
    // of the SchemaID as hex; "e7000711" for kSceneFromGLBSchema 0xF11ECA55E7000711,
    // distinct from scene-from-JSON's "e7000710"). We match on that first so a node
    // renamed via its `name` param still resolves. The Scene-from-GLB schema declares
    // no name/source_path param, so its DisplayName is deterministically the schema
    // label "Scene from GLB" (engine display_name fallback) — a reliable secondary
    // match that also survives a schema-id renumber on the engine side.
    private static bool IsSceneFromGlbNode(AssetGraphNodeCardViewModel node)
    {
        return string.Equals(
                node.SchemaLabel,
                SceneFromGlbSchemaLabel,
                System.StringComparison.Ordinal)
            || string.Equals(
                node.DisplayName,
                SceneFromGlbDisplayName,
                System.StringComparison.Ordinal);
    }

    // A scene-source reference/descriptor was assigned or cleared in the inspector
    // (issue #213): the runtime re-grafted, so re-merge its grafted children into
    // the scene tree under their hosts. The merge re-queries the runtime and
    // de-dupes its own previous grafts, so calling it after every change is safe.
    private void OnInspectorSceneSourceChanged()
    {
        SceneTree.MergeGraftedNodes();
    }

    // A node param was applied in the inspector (#218 Phase 3). The edit is in
    // the engine draft; re-pull the graph from the live session so the cached
    // node card picks up the new value. The reload preserves layout and
    // re-selects the node, which re-inspects the refreshed card — so the applied
    // value sticks instead of reverting to the schema default on re-selection.
    private void OnInspectorAssetGraphNodeParamApplied()
    {
        AssetGraph.RefreshFromSession();
    }

    // A named reroute changed in the inspector; re-project every open graph pane so the
    // badges and wire-hiding refresh (the reroute model is shared across panes).
    private void OnInspectorRerouteChanged()
    {
        AssetGraph.ReapplyProjection();
        if (_assetGraphDock?.VisibleDockables is { } dockables)
        {
            foreach (var dockable in dockables)
            {
                if (dockable is Document { Context: AssetGraphEditorPaneViewModel pane })
                {
                    pane.ReapplyProjection();
                }
            }
        }
    }

    // The stable schema discriminator for the "Scene from GLB" asset-graph node
    // (issue #213): the engine serializes node.schema_label = schema_tail(schema) =
    // low 32 bits of the SchemaID as 8 hex digits. kSceneFromGLBSchema is
    // 0xF11ECA55E7000711, so its tail is "e7000711". Primary, rename-proof match.
    private const string SceneFromGlbSchemaLabel = "e7000711";

    // The engine's schema_display_name_view of the Scene-from-GLB schema. Because
    // that schema declares no name/source_path param, a node's DisplayName falls
    // back to this label deterministically — the secondary match that survives a
    // schema-id renumber. Also the name shown on the node card.
    private const string SceneFromGlbDisplayName = "Scene from GLB";
}
