namespace Wozzits.Editor.ViewModels.EditorPanes;

using System.Collections.ObjectModel;
using System.IO;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;

public sealed class SceneTreeEditorPaneViewModel : ViewModelBase
{
    // Default subfolder under the project root where exported scenelets land.
    public const string SceneletsFolderName = "scenelets";

    private SceneTreeNodeViewModel? _selectedNode;
    private readonly IWozzitsEngineEditorSession? _editorSession;
    private readonly string _projectDirectory;
    private readonly Action<string>? _log;

    // The graft roots currently merged into the tree (issue #213), so a re-merge
    // (after a re-assign/clear) can drop the previous grafts before adding the
    // fresh ones — otherwise re-assigning would duplicate the sub-tree. Each entry
    // is the injected root VM; removing it drops its whole sub-tree.
    private readonly List<SceneTreeNodeViewModel> _mergedGraftRoots = [];

    public SceneTreeEditorPaneViewModel(
        IWozzitsEngineEditorSession? editorSession = null,
        string? projectDirectory = null,
        Action<string>? log = null)
    {
        _editorSession = editorSession;
        _projectDirectory = projectDirectory ?? string.Empty;
        _log = log;
    }

    public event Action<SceneTreeNodeViewModel?>? SelectedNodeChanged;

    public ObservableCollection<SceneTreeNodeViewModel> Nodes { get; } = [];

    public string EmptyState { get; private set; } = "No scene loaded.";

    public bool HasScene => Nodes.Count > 0;

    public bool HasNoScene => !HasScene;

    // Scene-tree structural edits (add child / reparent / delete / export) run
    // against the live viewport runtime, so they are only available while it is
    // running. Evaluated live (the runtime can stop asynchronously when its
    // window is closed, with no push notification) — the context menu reads it
    // on open to disable its items, and each edit method re-checks it via
    // RequireRuntime so the drag-drop and Delete-key paths are gated too.
    public bool CanEditScene => _editorSession?.IsRuntimeRunning ?? false;

    // Gate a runtime-requiring scene-tree edit: returns true when the viewport is
    // running, otherwise logs why the edit was skipped and returns false. This is
    // the single place the "requires the running viewport" message is written, so
    // every editing path (menu, Delete key, drag-drop) reports it consistently
    // rather than silently doing nothing.
    private bool RequireRuntime(string action)
    {
        if (CanEditScene)
        {
            return true;
        }

        _log?.Invoke(
            $"[editor] {action} requires the running viewport, which is not "
            + "running; reopen it (Restart Viewport) and try again.");
        return false;
    }

    public SceneTreeNodeViewModel? SelectedNode
    {
        get => _selectedNode;
        private set
        {
            if (ReferenceEquals(_selectedNode, value))
            {
                return;
            }

            _selectedNode = value;
            OnPropertyChanged();
            SelectedNodeChanged?.Invoke(_selectedNode);
        }
    }

    public void LoadSnapshot(EngineSceneSnapshotResponse? scene)
    {
        SetSelectedNode(null);
        Nodes.Clear();
        // The previous tree (and any grafts merged into it) is gone.
        _mergedGraftRoots.Clear();

        if (scene is null)
        {
            EmptyState = "No scene loaded.";
        }
        else if (!scene.Ok)
        {
            EmptyState = $"Could not load scene: {scene.Error}";
        }
        else
        {
            foreach (var node in scene.Snapshot.Roots)
            {
                Nodes.Add(new SceneTreeNodeViewModel(node));
            }

            EmptyState = Nodes.Count == 0
                ? "Scene has no nodes."
                : string.Empty;
        }

        OnPropertyChanged(nameof(EmptyState));
        OnPropertyChanged(nameof(HasScene));
        OnPropertyChanged(nameof(HasNoScene));

        // The grafted "Subtree from asset" children are merged separately (issue
        // #213): the caller (MainWindowViewModel) invokes MergeGraftedNodes once
        // the runtime is up, off the first-paint path, since that query blocks on
        // the engine thread.
    }

    // Merge the live runtime's grafted scene nodes (issue #213) under their host
    // nodes in the JSON-sourced tree. Defensive + idempotent: previously-merged
    // grafts are removed first (so a re-assign/clear does not duplicate), then
    // each returned root is appended under the authored host its ParentId names.
    // A missing host or an empty result simply leaves the tree as-is — authored
    // nodes keep their full data; this only ADDS grafts.
    public void MergeGraftedNodes()
    {
        if (_editorSession is null)
        {
            return;
        }

        // Drop the prior grafts first (each tracked root removes its sub-tree).
        if (_mergedGraftRoots.Count > 0)
        {
            foreach (var graft in _mergedGraftRoots)
            {
                RemoveFromTree(graft);
            }
            _mergedGraftRoots.Clear();
        }

        var grafted = _editorSession.LoadGraftedSceneNodes();
        var changed = false;
        foreach (var root in grafted.Roots)
        {
            // The graft root carries its host's id as ParentId; find that host in
            // the authored tree. Skip a graft with no/unknown host (defensive).
            if (string.IsNullOrEmpty(root.ParentId))
            {
                continue;
            }
            var host = FindNode(Nodes, root.ParentId);
            if (host is null)
            {
                continue;
            }

            var injected = new SceneTreeNodeViewModel(root, isInstanced: true);
            host.Children.Add(injected);
            _mergedGraftRoots.Add(injected);
            changed = true;
        }

        if (changed)
        {
            OnPropertyChanged(nameof(HasScene));
            OnPropertyChanged(nameof(HasNoScene));
        }
    }

    // Public lookup for cross-pane consumers (the inspector's effective-
    // render-program ancestor walk, issue #230): the tree node with `id`, or
    // null if none.
    public SceneTreeNodeViewModel? FindNodeById(string id) =>
        FindNode(Nodes, id);

    // Depth-first lookup of the tree node with `id`, or null if none.
    private static SceneTreeNodeViewModel? FindNode(
        IReadOnlyList<SceneTreeNodeViewModel> nodes,
        string id)
    {
        foreach (var node in nodes)
        {
            if (node.Id == id)
            {
                return node;
            }
            var found = FindNode(node.Children, id);
            if (found is not null)
            {
                return found;
            }
        }
        return null;
    }

    public void SelectNode(SceneTreeNodeViewModel node)
    {
        ArgumentNullException.ThrowIfNull(node);
        SetSelectedNode(node);
    }

    // Clear the scene-tree selection + its highlight (e.g. when the asset graph
    // takes the active selection). Raises SelectedNodeChanged(null) only if a
    // node was selected.
    public void ClearSelection()
    {
        SetSelectedNode(null);
    }

    private void SetSelectedNode(SceneTreeNodeViewModel? node)
    {
        if (ReferenceEquals(_selectedNode, node))
        {
            return;
        }

        if (_selectedNode is not null)
        {
            _selectedNode.IsSelected = false;
        }

        SelectedNode = node;

        if (SelectedNode is not null)
        {
            SelectedNode.IsSelected = true;
        }
    }

    // Add a new child node under `parent` (null => top level) via the engine,
    // then insert it into the tree with the engine-minted id and select it.
    public void AddChild(SceneTreeNodeViewModel? parent)
    {
        if (_editorSession is null)
        {
            return;
        }
        if (!RequireRuntime("Add child"))
        {
            return;
        }

        var response = _editorSession.AddChildNode(parent?.Id ?? string.Empty);
        if (!response.Ok)
        {
            EmptyState = response.Error;
            OnPropertyChanged(nameof(EmptyState));
            return;
        }

        var added = new SceneTreeNodeViewModel(new EngineSceneNode
        {
            Id = response.NodeId,
            ParentId = parent?.Id,
            Kind = "node",
            Visible = true,
            // A freshly added node has the engine's identity local transform.
            // Populate it so its transform pane shows immediately: the inspector
            // hides the pane while Transform is null (SetTransformFields sets
            // HasTransform = transform is not null), which is why a new node's
            // transform used to appear only after an editor reload re-read the
            // snapshot. Reload still re-reads the engine's authoritative value.
            Transform = IdentitySceneTransform(),
        });

        if (parent is null)
        {
            Nodes.Add(added);
        }
        else
        {
            parent.Children.Add(added);
        }

        OnPropertyChanged(nameof(HasScene));
        OnPropertyChanged(nameof(HasNoScene));
        SelectNode(added);
    }

    // The identity local transform the engine assigns a newly added node, mirrored
    // client-side so the inspector shows the transform pane right away. Display
    // strings match the identity values; a reload re-reads the engine's formatting.
    private static EngineSceneTransform IdentitySceneTransform() => new()
    {
        Translation = [0.0, 0.0, 0.0],
        RotationQuaternion = [0.0, 0.0, 0.0, 1.0],
        RotationEulerDegrees = [0.0, 0.0, 0.0],
        Scale = [1.0, 1.0, 1.0],
        Display = new EngineSceneTransformDisplay
        {
            TranslationX = "0",
            TranslationY = "0",
            TranslationZ = "0",
            RotationX = "0",
            RotationY = "0",
            RotationZ = "0",
            ScaleX = "1",
            ScaleY = "1",
            ScaleZ = "1",
        },
    };

    // Reparent `node` under `newParent` (null => top level) via the engine, then
    // move it in the tree. Rejects dropping a node onto itself or its own
    // descendant; the engine re-validates.
    public void Reparent(
        SceneTreeNodeViewModel node,
        SceneTreeNodeViewModel? newParent)
    {
        if (_editorSession is null || node is null)
        {
            return;
        }
        if (newParent is not null && node.IsSelfOrDescendant(newParent))
        {
            return;
        }
        if (!RequireRuntime("Reparent"))
        {
            return;
        }

        var response = _editorSession.ReparentNode(
            node.Id,
            newParent?.Id ?? string.Empty);
        if (!response.Ok)
        {
            EmptyState = response.Error;
            OnPropertyChanged(nameof(EmptyState));
            return;
        }

        RemoveFromTree(node);
        if (newParent is null)
        {
            Nodes.Add(node);
        }
        else
        {
            newParent.Children.Add(node);
        }
        node.ParentId = newParent?.Id;
        SelectNode(node);
    }

    // Whether `node` can move earlier / later in DRAW order among its same-parent
    // siblings (false at the ends of its sibling group). Drives the context
    // menu's enabled state.
    public bool CanMoveUp(SceneTreeNodeViewModel node)
    {
        var siblings = SiblingsOf(node);
        return siblings is not null && siblings.IndexOf(node) > 0;
    }

    public bool CanMoveDown(SceneTreeNodeViewModel node)
    {
        var siblings = SiblingsOf(node);
        if (siblings is null)
        {
            return false;
        }
        var index = siblings.IndexOf(node);
        return index >= 0 && index < siblings.Count - 1;
    }

    // Move `node` one slot earlier (up) / later (down) in DRAW order among its
    // same-parent siblings, via the engine reorder verb, then mirror the move in
    // the tree. Draw order only — nesting/transforms are parent_id-based and
    // untouched. No-op at the corresponding end of the sibling group.
    public void MoveUp(SceneTreeNodeViewModel node) => MoveWithinSiblings(node, -1);

    public void MoveDown(SceneTreeNodeViewModel node) => MoveWithinSiblings(node, +1);

    private void MoveWithinSiblings(SceneTreeNodeViewModel node, int delta)
    {
        if (_editorSession is null || node is null)
        {
            return;
        }
        if (!RequireRuntime(delta < 0 ? "Move up" : "Move down"))
        {
            return;
        }

        var siblings = SiblingsOf(node);
        if (siblings is null)
        {
            return;
        }
        var index = siblings.IndexOf(node);
        var target = index + delta;
        if (index < 0 || target < 0 || target >= siblings.Count)
        {
            return;  // already at that end of its sibling group
        }

        // The engine reorder places `node` just BEFORE `beforeId` in the flat
        // draw list. Move up => before the previous sibling. Move down => before
        // the sibling AFTER the next one (empty => move to the end / drawn last),
        // so `node` lands just past its next sibling.
        var beforeId = delta < 0
            ? siblings[index - 1].Id
            : (target + 1 < siblings.Count ? siblings[target + 1].Id : string.Empty);

        var response = _editorSession.ReorderNode(node.Id, beforeId);
        if (!response.Ok)
        {
            EmptyState = response.Error;
            OnPropertyChanged(nameof(EmptyState));
            return;
        }

        siblings.Move(index, target);
        SelectNode(node);
    }

    // The observable collection holding `node`'s siblings in draw order: the
    // top-level Nodes when it has no parent, else its parent's Children. Null if
    // the parent is not in the tree (defensive).
    private ObservableCollection<SceneTreeNodeViewModel>? SiblingsOf(
        SceneTreeNodeViewModel node)
    {
        if (node is null)
        {
            return null;
        }
        if (string.IsNullOrEmpty(node.ParentId))
        {
            return Nodes;
        }
        return FindNodeById(node.ParentId)?.Children;
    }

    // Delete `node` (and its subtree) via the engine, then drop it from the tree.
    public void Remove(SceneTreeNodeViewModel node)
    {
        if (_editorSession is null || node is null)
        {
            return;
        }
        if (!RequireRuntime("Delete"))
        {
            return;
        }

        var response = _editorSession.RemoveNode(node.Id);
        if (!response.Ok)
        {
            EmptyState = response.Error;
            OnPropertyChanged(nameof(EmptyState));
            return;
        }

        // Clear the selection if it was inside the removed subtree.
        var selectionRemoved = SelectedNode is not null
            && node.IsSelfOrDescendant(SelectedNode);
        RemoveFromTree(node);
        if (selectionRemoved)
        {
            SetSelectedNode(null);
        }
        OnPropertyChanged(nameof(HasScene));
        OnPropertyChanged(nameof(HasNoScene));
    }

    // The default folder offered by the "Export subtree as scene…" save dialog: a
    // `scenelets/` subfolder under the project root, created on demand. Returns
    // null when there is no known project root (design-time / tests) or the folder
    // cannot be created, so the caller falls back to the platform default folder.
    public string? EnsureSceneletsDirectory()
    {
        if (string.IsNullOrWhiteSpace(_projectDirectory))
        {
            return null;
        }

        var scenelets = Path.Combine(_projectDirectory, SceneletsFolderName);
        try
        {
            Directory.CreateDirectory(scenelets);
        }
        catch (IOException)
        {
            return null;
        }
        catch (UnauthorizedAccessException)
        {
            return null;
        }
        return scenelets;
    }

    // The suggested file name the save dialog seeds for `node`:
    // "<nodeName>.scene.json", sanitized of path-invalid characters and falling
    // back to the node id when it has no distinct display name.
    public static string SuggestExportFileName(SceneTreeNodeViewModel node)
    {
        ArgumentNullException.ThrowIfNull(node);
        var stem = string.IsNullOrWhiteSpace(node.DisplayName)
            || node.DisplayName == node.Id
                ? node.Id
                : node.DisplayName;
        if (string.IsNullOrWhiteSpace(stem))
        {
            stem = "scenelet";
        }
        foreach (var invalid in Path.GetInvalidFileNameChars())
        {
            stem = stem.Replace(invalid, '_');
        }
        return $"{stem}.scene.json";
    }

    // Export `node` (its authored id) + its subtree to `outPath` (an absolute file
    // path the caller chose via the save dialog) through the engine, logging the
    // outcome the way the editor surfaces other engine-call results. The runtime
    // does the carving + write; this only plumbs the call. No-op when no node /
    // session.
    public void ExportSubtree(SceneTreeNodeViewModel? node, string outPath)
    {
        if (_editorSession is null || node is null
            || string.IsNullOrWhiteSpace(outPath))
        {
            return;
        }
        if (!RequireRuntime("Export subtree"))
        {
            return;
        }

        var response = _editorSession.ExportSubtreeAsScene(node.Id, outPath);
        _log?.Invoke(response.Ok
            ? $"[editor] Exported subtree '{node.Id}' to {outPath}"
            : $"[editor] Export subtree failed: {response.Error}");
    }

    private bool RemoveFromTree(SceneTreeNodeViewModel node)
    {
        return Nodes.Remove(node) || RemoveFromChildren(Nodes, node);
    }

    private static bool RemoveFromChildren(
        ObservableCollection<SceneTreeNodeViewModel> siblings,
        SceneTreeNodeViewModel node)
    {
        foreach (var sibling in siblings)
        {
            if (sibling.Children.Remove(node)
                || RemoveFromChildren(sibling.Children, node))
            {
                return true;
            }
        }
        return false;
    }
}

public sealed class SceneTreeNodeViewModel : ViewModelBase
{
    private bool _isSelected;
    private string _displayName = string.Empty;
    private string? _parentId;

    public SceneTreeNodeViewModel(EngineSceneNode node, bool isInstanced = false)
    {
        Id = node.Id;
        DisplayName = node.DisplayName;
        ParentId = node.ParentId;
        Kind = node.Kind;
        KindLabel = node.Kind;
        Visible = node.Visible;
        RenderableSource = node.RenderableSource;
        Transform = node.Transform;
        Camera = node.Camera;
        Renderable = node.Renderable;
        SceneSource = node.SceneSource;
        SceneSourceNodeId = node.SceneSourceNodeId;
        GeometryNodeId = node.GeometryNodeId;
        RenderProgramNodeId = node.RenderProgramNodeId;
        Collision = node.Collision;
        Motion = node.Motion;
        AudioSource = node.AudioSource;
        RenderableBindings = node.RenderableBindings;
        RenderableConstants = node.RenderableConstants;
        Components = node.Components;
        Behaviors = node.Behaviors;
        IsInstanced = isInstanced;
        Children = new ObservableCollection<SceneTreeNodeViewModel>(
            node.Children.Select(
                child => new SceneTreeNodeViewModel(child, isInstanced)));
    }

    public string Id { get; }

    public string DisplayName
    {
        get => _displayName;
        internal set
        {
            if (SetProperty(ref _displayName, value))
            {
                OnPropertyChanged(nameof(DisplayText));
            }
        }
    }

    // Tree display: "id:label" (e.g. "14:terrain mesh"). Falls back to just the
    // id when the node has no distinct label yet (engine-side, an unnamed node's
    // name defaults to its id).
    public string DisplayText =>
        string.IsNullOrEmpty(DisplayName) || DisplayName == Id
            ? Id
            : $"{Id}:{DisplayName}";

    public string? ParentId
    {
        get => _parentId;
        internal set => SetProperty(ref _parentId, value);
    }

    public string Kind { get; }

    public string KindLabel { get; }

    // Settable so the inspector's live visibility edit is mirrored back onto the
    // tree node (like DisplayName/Transform) — otherwise re-selecting the node
    // re-reads the stale snapshot value and the checkbox reverts (issue #213).
    public bool? Visible { get; internal set; }

    public EngineSceneRenderableSource RenderableSource { get; }

    // Settable so live inspector edits can refresh the cached model in step with
    // the runtime push -- otherwise re-selecting a node repopulates the inspector
    // from the startup snapshot and the edit appears to revert (fix 1).
    public EngineSceneTransform? Transform { get; internal set; }

    // Settable so the inspector's live add/remove of the camera component keeps
    // reselection consistent: SetComponentFields derives HasCameraComponent from
    // this, so a read-only field made it revert to the snapshot on reselect
    // (mirrors Behaviors/Renderable).
    public EngineSceneCamera? Camera { get; internal set; }

    // Settable so the inspector's live add/remove of the renderable reference keeps
    // reselection consistent: Inspect derives HasRenderableReference from this, so a
    // read-only field left it reverting to the startup snapshot on the next select
    // (mirrors Transform/Visible).
    public EngineSceneRenderable? Renderable { get; internal set; }

    // GLB scene-source descriptor summary (issue #213 Phase 2 snapshot). Settable
    // so the inspector's live import/clear keeps reselection consistent without a
    // snapshot reload (mirrors Transform).
    public EngineSceneNodeSceneSource? SceneSource { get; internal set; }

    // Authored render-binding refs (issue #213), settable so the inspector's live
    // add/remove mirrors back and reselection reflects the change instead of
    // reverting to the startup snapshot (mirrors Renderable): the "Subtree from
    // asset" node ref + the render-binding geometry/render-program node ids.
    public ulong? SceneSourceNodeId { get; internal set; }

    public ulong? GeometryNodeId { get; internal set; }

    public ulong? RenderProgramNodeId { get; internal set; }

    // Persisted Collision/Motion component field values (read-back gap fix).
    // Settable so the inspector's live edits mirror back onto the cached node and
    // an immediate reselect (before the next snapshot refresh) shows the edit
    // instead of reverting to the startup snapshot (mirrors Transform/Visible).
    public EngineSceneNodeCollision? Collision { get; internal set; }

    public EngineSceneNodeMotion? Motion { get; internal set; }

    // Persisted AudioSource component field values (read-back); settable so the
    // inspector's live edits mirror back onto the cached node (like Collision).
    public EngineSceneNodeAudioSource? AudioSource { get; internal set; }

    // Custom-renderable ingredients (issue #229/#230). Mutable so the
    // inspector's live binding/constant edits mirror back onto the cached node
    // and an immediate reselect shows them instead of reverting to the startup
    // snapshot (mirrors Behaviors/Collision).
    public List<EngineSceneRenderableBinding> RenderableBindings { get; internal set; }

    public List<EngineSceneRenderableConstant> RenderableConstants { get; internal set; }

    // Mutable so the inspector's live add/remove of optional components keeps
    // reselection consistent: SetComponentFields rebuilds the inspector rows from
    // this list, so a read-only list made added/removed components revert on
    // reselect (mirrors Behaviors).
    public List<EngineSceneComponent> Components { get; }

    // Authored behavior bindings on this node. Mutable so the inspector's
    // live add/remove keeps reselection consistent without a snapshot reload.
    public List<EngineSceneBehavior> Behaviors { get; }

    public ObservableCollection<SceneTreeNodeViewModel> Children { get; }

    public bool HasChildren => Children.Count > 0;

    // True for a runtime-grafted "Subtree from asset" node (issue #213): merged
    // into the tree from the live runtime, not authored in scene.json. These are
    // read-only — not savable and not directly editable as authored nodes — so a
    // row style can mark them and the editor must not treat them as authored.
    public bool IsInstanced { get; }

    // True if `candidate` is this node or anywhere in its subtree — used to
    // reject reparenting a node onto itself or one of its own descendants.
    public bool IsSelfOrDescendant(SceneTreeNodeViewModel candidate)
    {
        if (ReferenceEquals(candidate, this))
        {
            return true;
        }
        foreach (var child in Children)
        {
            if (child.IsSelfOrDescendant(candidate))
            {
                return true;
            }
        }
        return false;
    }

    public bool IsSelected
    {
        get => _isSelected;
        internal set => SetProperty(ref _isSelected, value);
    }
}
