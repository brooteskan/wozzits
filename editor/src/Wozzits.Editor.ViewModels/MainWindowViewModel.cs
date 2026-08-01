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
using Wozzits.Editor.ViewModels.EditorPanes.Minds;
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

    // A delegate rather than the launcher itself, in the same style as `dispatch`
    // above: it is the only seam a test can use to drive the play lifecycle without
    // a real engine host on disk.
    private readonly Func<string, Action<string>, Process?> _standaloneLauncher;
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
        FileLogSink? fileLogSink = null,
        Func<string, Action<string>, Process?>? standaloneLauncher = null)
    {
        _editorSession = editorSession;
        _editorSessionLifetime = editorSession as IDisposable;
        _dispatch = dispatch;
        _standaloneLauncher = standaloneLauncher
            ?? new StandaloneAppLauncher().Launch;
        _projectDirectory = projectDirectory ?? string.Empty;
        SaveAllCommand = new RelayCommand(SaveAll);
        RestartViewportCommand = new RelayCommand(RestartViewport, () => _editorSession is not null);
        AddInochiSharedAssetsCommand = new RelayCommand(
            AddInochiSharedAssets, () => _editorSession is not null);
        RebuildBehaviorsCommand = new AsyncRelayCommand(
            RebuildBehaviorsAsync,
            () => _editorSession is not null);
        LaunchStandaloneCommand = new RelayCommand(
            LaunchStandalone,
            () => !string.IsNullOrWhiteSpace(_projectDirectory)
                && !IsStandaloneRunning);
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
        RefreshMindsCommand = new RelayCommand(
            RefreshMinds, () => !string.IsNullOrWhiteSpace(_projectDirectory));
        NewMindCommand = new RelayCommand(
            NewMind, () => !string.IsNullOrWhiteSpace(_projectDirectory) && _assetGraphDock is not null);
        OpenMindCommand = new RelayCommand<MindFileInfo?>(
            OpenMind, _ => _assetGraphDock is not null);
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
        // "Statechart runner" card: the inspector pulls the project's charts on demand (a fresh
        // folder scan, not the lazily-filled menu list), so a node can be pointed at a chart.
        Inspector.SetStatechartsProvider(EnumerateStatechartFiles);
        // "Mind" card on a node's quantum_agent: pull the project's minds on demand.
        Inspector.SetMindsProvider(EnumerateMindFiles);
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
    public IRelayCommand AddInochiSharedAssetsCommand { get; }
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

    public IRelayCommand RefreshMindsCommand { get; }

    public IRelayCommand NewMindCommand { get; }

    public IRelayCommand<MindFileInfo?> OpenMindCommand { get; }

    public ObservableCollection<MindFileInfo> Minds { get; } = [];

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

    // Pause/resume the viewport simulation: the view stays live (it keeps re-presenting the
    // last frame) but agents / behaviors / time stop advancing, freeing that CPU. A no-op when
    // no viewport is running.
    private bool _simulationPaused;
    public bool SimulationPaused
    {
        get => _simulationPaused;
        set
        {
            if (SetProperty(ref _simulationPaused, value))
            {
                _editorSession?.SetSimulationPaused(value);
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
        // Report any edit the engine could not apply BEFORE writing (#308, A1-C6).
        // The mutation verbs are fire-and-forget: they return OK before the engine
        // thread resolves the node id, so an edit aimed at a node that a graph swap,
        // despawn or open_scene removed used to vanish with the caller told it
        // succeeded. Save is where that matters most -- we are about to persist a
        // state that does NOT contain the edit the user thinks they made.
        ReportDroppedEdits();
        // The asset-graph save can legitimately REFUSE: while a compile is
        // binding, the engine holds the session's draft and the session is busy
        // (#313, B4-C1 — saving through it used to write an empty graph over the
        // project's real one). Save All is an explicit user action, so a graph
        // that did not persist has to say so; discarding this result is how the
        // silent version looked from here.
        if (_editorSession?.SaveAssetGraph() is { Ok: false } graphSave)
        {
            AppendEditorLog($"[editor] Asset graph NOT saved: {graphSave.Error}");
        }
        // Save All is an explicit user action, so a scene that did not persist
        // has to say so — silence here reads as "saved" (typically: the viewport
        // was closed, so the engine holds no scene to write).
        if (_editorSession?.SaveScene() is { Ok: false } sceneSave)
        {
            AppendEditorLog($"[editor] Scene NOT saved: {sceneSave.Error}");
        }
        SaveSubGraphSidecar();
        SaveOpenStatecharts();
        SaveOpenMinds();
    }

    // Drain the engine's dropped-edit report and surface each line. TAKE semantics
    // engine-side, so this must log what it drains -- nothing else will show it.
    private void ReportDroppedEdits()
    {
        if (_editorSession is null)
        {
            return;
        }

        foreach (var dropped in _editorSession.TakeDroppedEdits())
        {
            AppendEditorLog($"[editor] Edit did NOT apply -- {dropped}");
        }
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

        var savedCharts = new Dictionary<string, string>(StringComparer.Ordinal);   // name -> compiled IR
        foreach (var dockable in _assetGraphDock.VisibleDockables)
        {
            if (dockable is Document { Context: StatechartDocumentViewModel document } && document.IsDirty)
            {
                try
                {
                    document.Save();
                    // ValidatedIr, not CompiledIr (D3-C29): Save() only runs
                    // EmitValidated when the CHART changed, so a layout-only save
                    // -- a pan or a zoom -- reaches here with IR that may still be
                    // refused by the engine. This is the persisting reader
                    // CompiledIr's comment names, so the validation belongs here.
                    savedCharts[document.Name] = document.ValidatedIr();
                    AppendEditorLog($"[editor] Saved statechart '{document.Name}'");
                }
                catch (StatechartFormatException ex)
                {
                    // Structurally invalid: Save wrote nothing and the document stays dirty.
                    // Staying OUT of savedCharts is the load-bearing half -- otherwise the
                    // refresh below would push IR the engine refuses into every attached
                    // runner and into the scenelet FILES, so one broken chart would silently
                    // stop the behaviour everywhere it runs, including in spawned actors.
                    AppendEditorLog($"[editor] Statechart '{document.Name}' NOT saved: {ex.Message}");
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
                {
                    AppendEditorLog($"[editor] Statechart save failed for '{document.Name}': {ex.Message}");
                }
            }
        }

        RefreshAttachedRunners(savedCharts);
    }

    // Re-embed a just-saved chart into every scene node that runs it, so an edited chart takes
    // effect without re-attaching. A statechart_runner holds a COMPILED snapshot (chart_ir), not a
    // live file reference; matching by config `chart` (the file-stem name a runner stores), we push
    // the fresh IR through the live session and re-save the scene. (Applies while the viewport is
    // running -- the usual edit/save/restart loop; the runner rebuilds on the next self.start.)
    private void RefreshAttachedRunners(IReadOnlyDictionary<string, string> savedCharts)
    {
        if (_editorSession is null || savedCharts.Count == 0)
        {
            return;
        }

        var refreshed = 0;

        void Walk(IEnumerable<SceneTreeNodeViewModel> nodes)
        {
            foreach (var node in nodes)
            {
                foreach (var behavior in node.Behaviors)
                {
                    if (behavior.Module != "statechart_runner")
                    {
                        continue;
                    }

                    var chartName = behavior.Config.FirstOrDefault(c => c.Name == "chart")?.Value;
                    if (chartName is not null && savedCharts.TryGetValue(chartName, out var ir))
                    {
                        // Report what the engine actually did (D3-C8). This used to
                        // discard the response and log "Refreshed runner on 'X'"
                        // unconditionally -- from inside Save All, whose own body
                        // twenty lines up checks SaveScene precisely because
                        // "silence here reads as saved".
                        var response = _editorSession.SetNodeBehaviorConfig(
                            node.Id, behavior.Id, "chart_ir", "string", ir);
                        if (response.Ok)
                        {
                            refreshed++;
                            AppendEditorLog($"[editor] Refreshed runner on '{node.DisplayName}' from chart '{chartName}'");
                        }
                        else
                        {
                            AppendEditorLog(
                                $"[editor] Runner on '{node.DisplayName}' NOT refreshed "
                                + $"from chart '{chartName}': {response.Error}");
                        }
                    }
                }

                Walk(node.Children);
            }
        }

        Walk(SceneTree.Nodes);

        if (refreshed > 0 && _editorSession.SaveScene() is { Ok: false } save)
        {
            AppendEditorLog(
                $"[editor] Scene NOT saved after refreshing {refreshed} runner(s): {save.Error}");
        }

        // The walk above only covers the OPEN scene. A runner in a scenelet that is not the
        // open document keeps a stale chart_ir -- and spawned tanks read scenelets, so that is
        // the copy gameplay actually runs. Refresh those files on disk too.
        RefreshSceneletEmbedded("statechart_runner", "chart", "chart_ir", savedCharts);
    }

    // Re-embed just-saved charts/minds into behaviours in the project's scenelet FILES (the
    // templates the spawner instantiates). The in-scene refreshes can't see them; without this,
    // editing a chart or mind never reaches a spawned actor and its doctrine/reward work
    // silently runs an old compiled copy. Behaviours already carrying the current IR (e.g. in a
    // scenelet that is also the open scene) are skipped, so this never double-writes.
    private void RefreshSceneletEmbedded(
        string module, string nameKey, string irKey,
        IReadOnlyDictionary<string, string> savedByName)
    {
        if (_editorSession is null || savedByName.Count == 0)
        {
            return;
        }

        // Enumerate through the engine's own catalog rather than walking a
        // scenelets/ folder: where scenelets live is the engine's to know, and
        // the catalog already reports the resource-relative paths the verb takes.
        foreach (var scenelet in _editorSession.GetSceneletCatalog())
        {
            foreach (var (name, ir) in savedByName)
            {
                var applied = _editorSession.SetSceneFileBehaviorConfig(
                    scenelet.Path, module, nameKey, name, irKey, ir);
                if (!applied.Ok)
                {
                    AppendEditorLog(
                        $"[editor] Scenelet refresh failed for '{scenelet.Name}': {applied.Error}");
                    continue;
                }

                // 0 => the file already carried this IR and was not rewritten
                // (e.g. a scenelet that is also the open scene, already
                // refreshed in place). Stay quiet about a no-op.
                if (applied.UpdatedCount > 0)
                {
                    AppendEditorLog(
                        $"[editor] Refreshed {applied.UpdatedCount} {module} in scenelet '{scenelet.Name}'");
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
            $"[editor] Scenelet list: {Scenelets.Count} scenelet(s) "
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
                $"[editor] Open scenelet '{scenelet.Name}' failed: {result.Error}");
            return;
        }
        EditingPrefabName = scenelet.Name;
        AppendEditorLog($"[editor] Editing scenelet '{scenelet.Name}'.");
        ReloadSceneFromRuntime();
    }

    // A suggested unused name to pre-fill the New Scenelet prompt (untitled1, untitled2, …).
    // Sourced from the engine-published catalog rather than by probing the filesystem: the
    // editor does not know where scenelets live. This is only a suggestion -- the engine is
    // the authority on collisions and rejects a duplicate name outright.
    public string NextSceneletName()
    {
        for (int i = 1; ; i++)
        {
            var name = "untitled" + i;
            if (!Scenelets.Any(
                    s => string.Equals(s.Name, name, StringComparison.OrdinalIgnoreCase)))
            {
                return name;
            }
        }
    }

    // Create a fresh, minimal scenelet (one empty root node) named `name` and open it for
    // editing -- the Scenelets menu is the one place scenelets are created/opened (the view
    // prompts for the name). The ENGINE mints the document and decides where it goes,
    // returning the resource-relative path (issue #271); opening re-scans the scenelets dir,
    // so the new file registers as a spawnable prefab AND joins the catalog immediately.
    public void CreateScenelet(string name)
    {
        if (_editorSession is null)
        {
            return;
        }

        // Name validation (empty, path separators, reserved characters) lives with the
        // engine verb, so the rules cannot drift from what it will actually accept.
        var created = _editorSession.CreateScenelet(name.Trim());
        if (!created.Ok)
        {
            AppendEditorLog($"[editor] Could not create scenelet: {created.Error}");
            return;
        }

        OpenScenelet(new SceneletInfo(name.Trim(), created.Path));
        RefreshScenelets();
        AppendEditorLog($"[editor] Created scenelet '{name.Trim()}'");
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
        // A fresh runtime starts unpaused, so keep the toggle in sync.
        SimulationPaused = false;
        // ...and unprofiled, so PUSH the profiling toggle rather than assigning it
        // (D3-C16). Assignment is not enough: SetProperty short-circuits on an
        // unchanged value, so `FrameProfilingEnabled = FrameProfilingEnabled` sends
        // nothing. That short-circuit is also why the obvious user recovery failed
        // -- clicking a menu item that is already checked does nothing -- leaving
        // the menu claiming profiling was on while the new runtime had it off,
        // recoverable only by toggling off and on again.
        _editorSession?.SetFrameProfiling(FrameProfilingEnabled);
        // The runtime is back (or gone): re-enable the inspector's edit surface.
        // The scene tree self-heals (it re-checks on context-menu open).
        Inspector.RefreshEditAvailability();
    }

    // "Add Inochi shared assets" (inochi S2c item 6): provision the shared puppet
    // render program subgraph once per project via the engine authoring routine
    // (VS/PS shaders -> PuppetProgram). Idempotent. Puppet renderables then wire
    // their program port to the returned program node.
    private void AddInochiSharedAssets()
    {
        if (AssetGraph.AddInochiSharedAssets())
        {
            AppendEditorLog(
                "[editor] Inochi shared assets ready — see the \"Inochi Shared "
                + "Assets\" sub-graph in the asset graph (drill in for the puppet "
                + "program + shaders).");
        }
        else
        {
            AppendEditorLog(
                "[editor] Add Inochi shared assets failed: "
                + (string.IsNullOrEmpty(AssetGraph.LastEditError)
                    ? "unknown error"
                    : AssetGraph.LastEditError));
        }
    }

    // Launch the project as a SEPARATE process (the shipped-app play path),
    // distinct from the in-process resident viewport. The standalone loads the
    // project's behavior-module DLLs, so behaviors play faithfully. The play
    // runs in its own window; closing the editor closes the host's stdin pipe,
    // which ends the play gracefully.
    // True while a play we launched is still alive. The handle is the only way
    // to know: the play is a separate process, so nothing else reports it.
    private bool IsStandaloneRunning
    {
        get
        {
            if (_standaloneProcess is null)
            {
                return false;
            }

            try
            {
                return !_standaloneProcess.HasExited;
            }
            catch (InvalidOperationException)
            {
                return false;  // never started
            }
            catch (System.ComponentModel.Win32Exception)
            {
                return false;  // handle no longer queryable — treat as gone
            }
        }
    }

    private void LaunchStandalone()
    {
        // SINGLE INSTANCE (#313, B4-C13). CanExecute only tested that a project
        // was open, so a second click spawned a second full engine -- its own
        // GPU device, its own copies of every behavior DLL -- and overwrote
        // _standaloneProcess, dropping the only handle to the first one. After
        // that the first play could not be tracked or stopped at all, and the
        // launcher's own doc comment ("returns the started Process so the caller
        // can hold a reference") shows the handle was meant to be used.
        //
        // NOT a fix for Shutdown() leaving the process alone: that is
        // deliberate and documented -- closing the editor closes the host's
        // stdin pipe, which ends the play gracefully.
        if (IsStandaloneRunning)
        {
            AppendEditorLog(
                "[editor] Play is already running; close it before starting "
                + "another.");
            return;
        }

        // Release the handle of a play that has since exited before taking a
        // new one; Process owns an OS handle and dropping it silently leaks
        // one per play for the editor's lifetime.
        _standaloneProcess?.Dispose();
        _standaloneProcess = _standaloneLauncher(
            _projectDirectory,
            AppendEditorLog);
        LaunchStandaloneCommand.NotifyCanExecuteChanged();

        // ...and announce the OTHER end of the transition (D3-P045). Making
        // CanExecute depend on IsStandaloneRunning gave the command a state change
        // that nothing signalled: the launcher's own Exited handler only logs, so
        // running -> exited never raised CanExecuteChanged and the menu item kept
        // its cached IsEnabled=false. Whether the user could recover depended
        // entirely on whether Avalonia happened to re-query on submenu reopen --
        // and if it does not, Play is dead for the rest of the editor session.
        if (_standaloneProcess is not null)
        {
            _standaloneProcess.Exited += OnStandaloneExited;

            // Subscribing after Start leaves a gap: a play that dies immediately
            // (a stale WOZZITS_EDITOR_HOST, a missing DLL) can raise Exited before
            // we attach, and the event does not replay for a late subscriber.
            if (!IsStandaloneRunning)
            {
                OnStandaloneExited(this, EventArgs.Empty);
            }
        }
    }

    // Process.Exited arrives on a threadpool thread; CanExecuteChanged drives UI
    // state, so it goes through the same dispatcher hop as the log flush.
    private void OnStandaloneExited(object? sender, EventArgs e)
    {
        if (_dispatch is not null)
        {
            _dispatch(LaunchStandaloneCommand.NotifyCanExecuteChanged);
        }
        else
        {
            LaunchStandaloneCommand.NotifyCanExecuteChanged();
        }
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

        BehaviorBuildResult result;
        try
        {
            result = await _behaviorBuilder.RebuildAsync(
                _projectDirectory,
                AppendEditorLog);
        }
        catch (Exception ex)
        {
            AppendEditorLog($"[editor] Behavior rebuild error: {ex.Message}");
            Inspector.SetBuildErrors([$"Behavior rebuild error: {ex.Message}"]);
            return;
        }

        switch (result.Outcome)
        {
            case BehaviorBuildOutcome.Failed:
                AppendEditorLog(
                    "[editor] Behavior rebuild failed; modules not reloaded.");
                // Put the diagnostics where the user is looking. The console has them
                // too, but it is full of live behavior output -- a failed build there
                // reads as "nothing happened", and the stale DLL gets blamed on the
                // editor instead of on the compile error.
                Inspector.SetBuildErrors(result.Errors);
                return;
            case BehaviorBuildOutcome.Skipped:
                // Nothing was built, so there is nothing to hot-reload.
                return;
        }

        Inspector.SetBuildErrors([]);   // built clean -> drop any stale failure
        var reload = _editorSession.ReloadBehaviorModules();
        AppendEditorLog(reload.Ok
            ? "[editor] Behavior modules reloaded."
            : $"[editor] Behavior reload skipped: {reload.Error}");

        // The DLLs just changed, so the config params they DECLARE may have too. The
        // inspector caches that schema, so without this a module that just grew params
        // keeps showing none until the editor restarts.
        Inspector.RefreshDeclaredParams();
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
        AssetGraph.GraphMutated += OnAssetGraphMutated;
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
        pane.GraphMutated += OnAssetGraphMutated;

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

    // Scan the project's authored statecharts (behavior/statecharts/*.sc.json). Side-effect-free,
    // so both the open menu (via RefreshStatecharts) and the inspector's runner dropdown (as a
    // provider) get a CURRENT list -- the runner section must not depend on the menu having been
    // opened to populate the shared Statecharts collection.
    private IReadOnlyList<StatechartFileInfo> EnumerateStatechartFiles()
    {
        var charts = new List<StatechartFileInfo>();
        if (string.IsNullOrWhiteSpace(_projectDirectory))
        {
            return charts;
        }

        var dir = Path.Combine(_projectDirectory, "behavior", "statecharts");
        if (!Directory.Exists(dir))
        {
            return charts;
        }

        foreach (var path in Directory.EnumerateFiles(dir, "*.sc.json").OrderBy(p => p))
        {
            var file = Path.GetFileName(path);
            var name = file.EndsWith(".sc.json", StringComparison.Ordinal) ? file[..^8] : file;
            charts.Add(new StatechartFileInfo(name, path));
        }

        return charts;
    }

    // Refill the shared Statecharts collection (bound to the open menu). Re-run when the menu
    // opens so newly-authored charts appear.
    private void RefreshStatecharts()
    {
        Statecharts.Clear();
        foreach (var chart in EnumerateStatechartFiles())
        {
            Statecharts.Add(chart);
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
        var name = FreshStatechartName(dir);
        var path = Path.Combine(dir, name + ".sc.json");

        var chart = new Chart { Name = name };
        chart.States.Add(new State { Id = "state1" });
        var region = new Region { Id = "region1", Initial = "state1" };
        region.States.Add("state1");
        chart.Regions.Add(region);

        try
        {
            // INSIDE the try (D3-P063). This is the first filesystem call on the
            // path -- the guard above only tests two strings -- and it throws
            // DirectoryNotFoundException / UnauthorizedAccessException on a removed
            // drive, a read-only share or a folder renamed while the editor was
            // open. Both derive from the families this catch already lists, so
            // sitting one line above it meant a menu click terminated the editor
            // where the intended behaviour was already written directly below.
            // (FreshStatechartName runs before this deliberately: it only does
            // File.Exists, which does not throw, and an absent folder holds no
            // charts, so the name it picks is right either way.)
            Directory.CreateDirectory(dir);

            // Validated like every other persisting write, even though the template above is
            // valid by construction: if a future edit to that template breaks it, the failure
            // should be a refused create here, not a runner that never starts at play time.
            File.WriteAllText(path, StatechartJson.EmitValidated(chart, indented: true));
        }
        catch (Exception ex) when (
            ex is IOException or UnauthorizedAccessException or StatechartFormatException)
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

        // The behavior-actuator catalog is device-free, so it loads without a live
        // viewport; a `call` effect uses it for its actuator + arg pickers.
        var actuatorCatalog = _editorSession?.LoadActuatorCatalog();
        var chartDocument = new StatechartDocumentViewModel(
            info.Name,
            info.Path,
            chart,
            actuatorCatalog is { Ok: true } ? actuatorCatalog.Actuators : null);
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

        // Bound a read op's slot picker even when it reads a REFERENCED agent, and let a
        // double-click on that ref open the mind it points to -- both need the scene (the
        // node this chart runs on -> its quantum_agent), which lives here, not in the pane.
        chartDocument.Dataflow.SetRefAgentResolvers(
            agent => ResolveRefAgentSlotCount(info.Name, agent),
            agent => OpenReferencedMind(info.Name, agent));
        // The project's minds a ref can point at directly (the picker on a ref agent card).
        chartDocument.Dataflow.SetMindChoices(
            EnumerateMindFiles().Select(m => m.Name).ToList());

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

    // ---- referenced-agent resolution (for the dataflow read-op slot picker + double-click) ----

    // A chart read op may reference an EXTERNAL agent (owned:false) whose decision slots live
    // on the scene node the chart runs on, not in the chart. Resolve that node's quantum_agent
    // so the slot picker can bound to 0..N-1. 0 when unresolvable: the chart isn't attached to a
    // node, the ref host isn't `self`, there's no matching quantum_agent, or its count is unknown.
    private int ResolveRefAgentSlotCount(string chartName, AgentDecl agent)
    {
        // Prefer the mind the ref names DIRECTLY -- chart-local, no scene node in the loop.
        if (agent.Mind.Length > 0)
        {
            return TryLoadMind(agent.Mind) is { } named ? named.Qubits.Count : 0;
        }
        // Otherwise resolve through the scene node the chart runs on (if any).
        var quantumAgent = ResolveReferencedQuantumAgent(chartName, agent);
        if (quantumAgent is null)
        {
            return 0;
        }
        // An attached graph mind: count its qubits. Otherwise the scalar `decisions` count.
        var mindName = quantumAgent.Config
            .FirstOrDefault(c => c.Name == QuantumAgentMindAttachment.ConfigMind)?.Value;
        if (!string.IsNullOrEmpty(mindName) && TryLoadMind(mindName) is { } mind)
        {
            return mind.Qubits.Count;
        }
        var decisions = quantumAgent.Config.FirstOrDefault(c => c.Name == "decisions")?.Value;
        return int.TryParse(decisions, out var n) && n > 0 ? n : 0;
    }

    // Double-clicking a REFERENCE agent card opens the .mind.json it points to (a graph mind).
    // Logs when there's nothing to open -- a scalar/plugin agent (like the tank) has no graph.
    private void OpenReferencedMind(string chartName, AgentDecl agent)
    {
        // The ref names a mind DIRECTLY? Open it -- no scene node needed. Otherwise fall back
        // to the mind attached to the quantum_agent on the node the chart runs on.
        var mindName = agent.Mind.Length > 0
            ? agent.Mind
            : ResolveReferencedQuantumAgent(chartName, agent)?.Config
                .FirstOrDefault(c => c.Name == QuantumAgentMindAttachment.ConfigMind)?.Value;
        if (string.IsNullOrEmpty(mindName))
        {
            AppendEditorLog(
                $"[editor] Agent '{agent.Id}' names no mind to open "
                + "(pick a mind on the ref, or attach one to its quantum_agent).");
            return;
        }
        var info = EnumerateMindFiles().FirstOrDefault(m => m.Name == mindName);
        if (info is null)
        {
            AppendEditorLog($"[editor] Mind '{mindName}' not found under behavior/minds.");
            return;
        }
        OpenMind(info);
    }

    // chart -> the scene node whose statechart_runner runs it -> the referenced quantum_agent
    // (matched by the ref's target label, or the first when unnamed). Only `self`-hosted refs
    // resolve for now (a binding-hosted ref lives on another node -- a follow-up).
    private EngineSceneBehavior? ResolveReferencedQuantumAgent(string chartName, AgentDecl agent)
    {
        if (!string.Equals(agent.Host, "self", StringComparison.Ordinal))
        {
            return null;
        }
        var host = FindChartHostNode(chartName);
        if (host is null)
        {
            return null;
        }
        var quantumAgents = host.Behaviors
            .Where(b => b.Module == QuantumAgentMindAttachment.Module)
            .ToList();
        if (quantumAgents.Count == 0)
        {
            return null;
        }
        return string.IsNullOrEmpty(agent.AgentName)
            ? quantumAgents[0]
            : quantumAgents.FirstOrDefault(b => b.Label == agent.AgentName);
    }

    private SceneTreeNodeViewModel? FindChartHostNode(string chartName)
    {
        SceneTreeNodeViewModel? Walk(IEnumerable<SceneTreeNodeViewModel> nodes)
        {
            foreach (var node in nodes)
            {
                foreach (var behavior in node.Behaviors)
                {
                    if (behavior.Module == "statechart_runner"
                        && behavior.Config.FirstOrDefault(c => c.Name == "chart")?.Value == chartName)
                    {
                        return node;
                    }
                }
                if (Walk(node.Children) is { } found)
                {
                    return found;
                }
            }
            return null;
        }
        return Walk(SceneTree.Nodes);
    }

    // Load an authored graph mind by name (to count its qubits); null if absent / unparseable.
    private Mind? TryLoadMind(string mindName)
    {
        try
        {
            var info = EnumerateMindFiles().FirstOrDefault(m => m.Name == mindName);
            return info is null ? null : MindJson.Load(File.ReadAllText(info.Path));
        }
        catch
        {
            return null;
        }
    }

    // ---- Minds menu (parallel to Statecharts): the project's authored .mind.json graphs ----

    private IReadOnlyList<MindFileInfo> EnumerateMindFiles()
    {
        var minds = new List<MindFileInfo>();
        if (string.IsNullOrWhiteSpace(_projectDirectory))
        {
            return minds;
        }

        var dir = Path.Combine(_projectDirectory, "behavior", "minds");
        if (!Directory.Exists(dir))
        {
            return minds;
        }

        foreach (var path in Directory.EnumerateFiles(dir, "*.mind.json").OrderBy(p => p))
        {
            var file = Path.GetFileName(path);
            var name = file.EndsWith(".mind.json", StringComparison.Ordinal) ? file[..^10] : file;
            minds.Add(new MindFileInfo(name, path));
        }

        return minds;
    }

    private void RefreshMinds()
    {
        Minds.Clear();
        foreach (var mind in EnumerateMindFiles())
        {
            Minds.Add(mind);
        }
    }

    // Create a fresh mind (one qubit, so it's a valid graph) in the project's minds folder
    // and open it. The Minds menu is the one place minds are created/opened.
    private void NewMind()
    {
        if (string.IsNullOrWhiteSpace(_projectDirectory) || _assetGraphDock is null)
        {
            return;
        }

        var dir = Path.Combine(_projectDirectory, "behavior", "minds");
        var name = FreshMindName(dir);
        var path = Path.Combine(dir, name + ".mind.json");

        var mind = new Mind { Name = name };
        mind.Qubits.Add(new MindQubit { Id = "q0" });

        try
        {
            // INSIDE the try (D3-P063) -- see the twin in NewStatechart above.
            Directory.CreateDirectory(dir);
            File.WriteAllText(path, MindJson.Emit(mind, indented: true));
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            AppendEditorLog($"[editor] Could not create mind '{name}': {ex.Message}");
            return;
        }

        RefreshMinds();
        OpenMind(new MindFileInfo(name, path));
        AppendEditorLog($"[editor] Created mind '{name}'");
    }

    private static string FreshMindName(string dir)
    {
        for (int i = 1; ; i++)
        {
            var name = "untitled" + i;
            if (!File.Exists(Path.Combine(dir, name + ".mind.json")))
            {
                return name;
            }
        }
    }

    // Open a mind as a document hosting its graph canvas. Re-focuses an already-open tab.
    private void OpenMind(MindFileInfo? info)
    {
        if (info is null || _assetGraphDock is null)
        {
            return;
        }

        var documentId = $"Mind_{info.Name}";
        var existing = _assetGraphDock.VisibleDockables?
            .FirstOrDefault(dockable => dockable.Id == documentId);
        if (existing is not null)
        {
            DockFactory.SetActiveDockable(existing);
            return;
        }

        Mind mind;
        try
        {
            mind = MindJson.Load(File.ReadAllText(info.Path));
        }
        catch (Exception ex)
        {
            AppendEditorLog($"[editor] Could not open mind '{info.Name}': {ex.Message}");
            return;
        }

        var mindDocument = new MindDocumentViewModel(info.Name, info.Path, mind);

        var document = new Document
        {
            Id = documentId,
            Title = info.Name,
            Context = mindDocument,
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

        mindDocument.Renamed = () =>
        {
            document.Title = mindDocument.Name;
            document.Id = $"Mind_{mindDocument.Name}";
            RefreshMinds();
        };
        mindDocument.Deleted = () =>
        {
            DockFactory.CloseDockable(document);
            RefreshMinds();
        };

        DockFactory.AddDockable(_assetGraphDock, document);
        DockFactory.SetActiveDockable(document);
    }

    // Save every open mind document that has edits: the mind back to its .mind.json (if the
    // model changed) + its hand-placed layout to a .mind.editor.json sidecar. Then re-embed the
    // fresh mind_ir into any quantum_agent already using that mind, so an edit takes effect.
    private void SaveOpenMinds()
    {
        if (_assetGraphDock?.VisibleDockables is null)
        {
            return;
        }

        var savedMinds = new Dictionary<string, string>(StringComparer.Ordinal);   // name -> compiled IR
        foreach (var dockable in _assetGraphDock.VisibleDockables)
        {
            if (dockable is Document { Context: MindDocumentViewModel document } && document.IsDirty)
            {
                try
                {
                    document.Save();
                    savedMinds[document.Name] = document.CompiledIr;
                    AppendEditorLog($"[editor] Saved mind '{document.Name}'");
                }
                catch (MindFormatException ex)
                {
                    // Same contract as the statechart half above, and it was missing
                    // here: a mind with no qubits (delete the last one -- there is no
                    // floor in the pane) throws out of Emit. Uncaught it left Save All
                    // -- a synchronous RelayCommand with no global handler behind it --
                    // and killed the editor, losing every other unsaved document.
                    // Staying OUT of savedMinds is the load-bearing half, exactly as
                    // for charts: the refresh below would otherwise push IR the engine
                    // refuses into every attached quantum_agent and scenelet FILE.
                    AppendEditorLog($"[editor] Mind '{document.Name}' NOT saved: {ex.Message}");
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
                {
                    AppendEditorLog($"[editor] Mind save failed for '{document.Name}': {ex.Message}");
                }
            }
        }

        RefreshAttachedMinds(savedMinds);
    }

    // Re-embed a just-saved mind into every quantum_agent that uses it (config `mind` matches),
    // pushing fresh mind_ir -- so an edited mind takes effect without re-attaching. Mirrors
    // RefreshAttachedRunners.
    private void RefreshAttachedMinds(IReadOnlyDictionary<string, string> savedMinds)
    {
        if (_editorSession is null || savedMinds.Count == 0)
        {
            return;
        }

        var refreshed = 0;

        void Walk(IEnumerable<SceneTreeNodeViewModel> nodes)
        {
            foreach (var node in nodes)
            {
                foreach (var behavior in node.Behaviors)
                {
                    if (behavior.Module != QuantumAgentMindAttachment.Module)
                    {
                        continue;
                    }

                    var mindName = behavior.Config
                        .FirstOrDefault(c => c.Name == QuantumAgentMindAttachment.ConfigMind)?.Value;
                    if (mindName is not null && savedMinds.TryGetValue(mindName, out var ir))
                    {
                        // Same contract as the runner twin above (D3-C8).
                        var response = _editorSession.SetNodeBehaviorConfig(
                            node.Id, behavior.Id, QuantumAgentMindAttachment.ConfigMindIr, "string", ir);
                        if (response.Ok)
                        {
                            refreshed++;
                            AppendEditorLog($"[editor] Refreshed quantum_agent on '{node.DisplayName}' from mind '{mindName}'");
                        }
                        else
                        {
                            AppendEditorLog(
                                $"[editor] quantum_agent on '{node.DisplayName}' NOT refreshed "
                                + $"from mind '{mindName}': {response.Error}");
                        }
                    }
                }

                Walk(node.Children);
            }
        }

        Walk(SceneTree.Nodes);

        if (refreshed > 0 && _editorSession.SaveScene() is { Ok: false } save)
        {
            AppendEditorLog(
                $"[editor] Scene NOT saved after refreshing {refreshed} quantum_agent(s): {save.Error}");
        }

        // Same scenelet blind-spot as runners: a quantum_agent in a scenelet (e.g. the tank's
        // own mind) keeps a stale mind_ir until refreshed on disk.
        RefreshSceneletEmbedded(
            QuantumAgentMindAttachment.Module,
            QuantumAgentMindAttachment.ConfigMind,
            QuantumAgentMindAttachment.ConfigMindIr,
            savedMinds);
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

        // Thread the "Geometry" picker (issue #213 increment 2) the same way:
        // mesh-producing nodes, so a scene node can pair a mesh with a render
        // program directly instead of grafting a GLB subtree.
        Inspector.SetAvailableGeometrySources(
            AssetGraph.Nodes
                .Where(IsMeshNode)
                .Select(node => new InspectorAssetGraphRefOptionViewModel(
                    node.Id,
                    node.DisplayName)));

        // Thread the "Collision" picker (terrain-stick track) the same way: the
        // inspector takes plain option data, filtered to Collision outputs.
        RefreshInspectorCollisionSources();

        // Thread the "Audio Source" picker (audio-track item 10), filtered to
        // audio-renderable outputs.
        RefreshInspectorAudioRenderableSources();

        // Thread the "Atmosphere" picker, filtered to Atmosphere outputs.
        RefreshInspectorAtmosphereSources();

        // Thread the "Environment" picker, filtered to FrameEnvironment outputs.
        RefreshInspectorEnvironmentSources();
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

    private void RefreshInspectorAtmosphereSources()
    {
        Inspector.SetAvailableAtmospheres(
            AssetGraph.Nodes
                .Where(IsAtmosphereNode)
                .Select(node => new InspectorAssetGraphRefOptionViewModel(
                    node.Id,
                    node.DisplayName)));
    }

    private void RefreshInspectorEnvironmentSources()
    {
        Inspector.SetAvailableEnvironments(
            AssetGraph.Nodes
                .Where(IsEnvironmentNode)
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

    // True when a node produces a Mesh the geometry binding can reference
    // (kAssetTypeMesh = 130 in type_extensions.h) — the mesh half of the #213
    // renderable binding, paired with a render program at assembly.
    private static bool IsMeshNode(AssetGraphNodeCardViewModel node) =>
        node.OutputPorts.Any(port => port.Type == MeshAssetTypeId);

    private const uint MeshAssetTypeId = 130;

    // True when a node produces a Collision asset the Collision component can
    // reference (kAssetTypeCollisionAsset = 150 in type_extensions.h).
    private static bool IsCollisionNode(AssetGraphNodeCardViewModel node) =>
        node.OutputPorts.Any(port => port.Type == CollisionAssetTypeId);

    private const uint CollisionAssetTypeId = 150;

    // True when a node produces an Atmosphere asset the Atmosphere component can
    // reference (kAssetTypeAtmosphere = 2289 in type_extensions.h).
    private static bool IsAtmosphereNode(AssetGraphNodeCardViewModel node) =>
        node.OutputPorts.Any(port => port.Type == AtmosphereAssetTypeId);

    private const uint AtmosphereAssetTypeId = 2289;

    // True when a node produces a FrameEnvironment asset the Environment component
    // can reference (kAssetTypeFrameEnvironment = 2290 in type_extensions.h).
    private static bool IsEnvironmentNode(AssetGraphNodeCardViewModel node) =>
        node.OutputPorts.Any(port => port.Type == EnvironmentAssetTypeId);

    private const uint EnvironmentAssetTypeId = 2290;

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
        ForEachAssetGraphPane(pane => pane.ReapplyProjection());
    }

    // A graph pane added or removed nodes. Every pane holds its own snapshot of the one
    // draft, so re-pull the others; the acting pane already reloaded itself and reloading
    // it again would discard the drop position it just pinned.
    private void OnAssetGraphMutated(AssetGraphEditorPaneViewModel sender)
    {
        ForEachAssetGraphPane(pane =>
        {
            if (!ReferenceEquals(pane, sender))
            {
                pane.RefreshFromSession();
            }
        });
    }

    // Visit every open asset-graph pane exactly once: the root canvas plus one drill-in tab
    // per opened sub-graph. The root pane is itself the dock's first document, so it comes
    // out of the walk -- the trailing add covers the headless case where no dock was built.
    private void ForEachAssetGraphPane(Action<AssetGraphEditorPaneViewModel> visit)
    {
        var visited = new HashSet<AssetGraphEditorPaneViewModel>();

        if (_assetGraphDock?.VisibleDockables is { } dockables)
        {
            foreach (var dockable in dockables)
            {
                if (dockable is Document { Context: AssetGraphEditorPaneViewModel pane }
                    && visited.Add(pane))
                {
                    visit(pane);
                }
            }
        }

        if (visited.Add(AssetGraph))
        {
            visit(AssetGraph);
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
