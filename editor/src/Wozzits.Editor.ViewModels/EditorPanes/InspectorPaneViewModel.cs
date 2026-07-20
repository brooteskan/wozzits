using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Linq;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Minds;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.ViewModels.EditorPanes;

public sealed class InspectorPaneViewModel : ViewModelBase
{
    private readonly IWozzitsEngineEditorSession? _editorSession;
    private readonly Action<string>? _log;
    private string _emptyState = "No scene or asset graph node selected.";
    private string _header = string.Empty;
    private InspectorSelectionKind _selectionKind;
    private DataflowNodeViewModel? _selectedStatechartNode;
    private StateNodeViewModel? _selectedStatechartState;
    private string _nodeId = string.Empty;
    private string _nodeName = string.Empty;
    private string _parentId = string.Empty;
    private bool _nodeVisible;
    private InspectorRenderLayerOption? _selectedRenderLayer;
    private string _renderableSource = string.Empty;
    private string _renderableSourceKind = string.Empty;
    private string _renderableAssetGraphNodeId = string.Empty;
    private bool _hasRenderableReference;
    // "Subtree from asset" section (issue #213 piece 2): the picked "Scene from GLB"
    // asset-graph node and the optimistic "Referencing: …" label. The reference is
    // session-local for now — reading it back from the snapshot is piece 3.
    private InspectorSceneSourceOptionViewModel? _selectedSceneSourceOption;
    private string _subtreeReferenceLabel = string.Empty;
    // Reveal flag for the "Subtree from asset" picker section, mirroring
    // _hasRenderableReference: the section is attached via "Add Component →
    // subtree_from_asset" (or revealed when a session reference already exists), not
    // shown unconditionally (issue #213 piece 2 review).
    private bool _hasSubtreeSection;
    // "Render program" section (issue #213): the render-program asset-graph node
    // assigned to this scene node, inherited down the tree to descendants without
    // their own. The engine applies it to the node's geometry — intrinsic for a
    // grafted GLB part, or supplied by a "Renderable" component otherwise. (The
    // geometry-by-ingredients picker was dropped: geometry now comes from the graft
    // or a pre-built renderable, so only the program is authored here.)
    private InspectorAssetGraphRefOptionViewModel? _selectedRenderProgramOption;
    private InspectorAssetGraphRefOptionViewModel? _selectedGeometryOption;
    private string _geometryReferenceLabel = string.Empty;
    private bool _hasGeometrySection;
    private bool _hasRenderableIngredientsSection;
    private string _renderableIngredientsHint = string.Empty;
    private Func<string, SceneTreeNodeViewModel?>? _sceneNodeLookup;
    private string _renderProgramReferenceLabel = string.Empty;
    private bool _hasRenderProgramSection;
    // "Collision" section (terrain-stick track): the Collision asset-graph node this
    // node references + a constrain-movement flag. Like render program, the picked
    // reference is session-local/optimistic for now (the snapshot does not yet
    // surface collision_asset_node_id / constrain_movement) — reading it back is a
    // deferred follow-up. The section is shown when the node HAS a collision
    // component (added/removed via the generic Add-Component / header ✕).
    private InspectorAssetGraphRefOptionViewModel? _selectedCollisionOption;
    private string _collisionReferenceLabel = string.Empty;
    private bool _hasCollisionComponent;
    private bool _collisionConstrainMovement;
    // "Atmosphere" section: the Atmosphere asset-graph node the frame's fog reads +
    // an enabled flag. One combined live seam (ref + enabled), like collision.
    private InspectorAssetGraphRefOptionViewModel? _selectedAtmosphereOption;
    private string _atmosphereReferenceLabel = string.Empty;
    private bool _hasAtmosphereComponent;
    private bool _atmosphereEnabled = true;
    // "Environment" section: the FrameEnvironment asset-graph node the frame's
    // environment reads + an enabled flag. One combined live seam, like atmosphere.
    private InspectorAssetGraphRefOptionViewModel? _selectedEnvironmentOption;
    private string _environmentReferenceLabel = string.Empty;
    private bool _hasEnvironmentComponent;
    private bool _environmentEnabled = true;
    // "Motion" section (terrain-stick track): the terrain-constraint fields of the
    // node's Motion component. Optimistic/session-local display, same as collision.
    private bool _hasMotionComponent;
    private bool _motionTerrainConstrained;
    private string _motionRideHeight = string.Empty;
    private string _motionFootprintRadius = string.Empty;
    private bool _motionAlignToSurface;
    private string _motionAlignmentStrength = string.Empty;
    // "Motion Filter" section (secondary-motion camera damping): per-DOF smoothing
    // + clamp of the node's driven transform. Optimistic/session-local display,
    // same pattern as Motion. Float fields are strings (parsed on commit).
    private bool _hasMotionFilterComponent;
    private bool _motionFilterEnabled = true;
    private string _motionFilterTranslationSmoothingX = string.Empty;
    private string _motionFilterTranslationSmoothingY = string.Empty;
    private string _motionFilterTranslationSmoothingZ = string.Empty;
    private bool _motionFilterTerrainFloor;
    private string _motionFilterTerrainFloorOffset = string.Empty;
    private string _motionFilterRollSmoothing = string.Empty;
    private bool _motionFilterRollLevel;
    private bool _motionFilterRollLimit;
    private string _motionFilterRollLimitMin = string.Empty;
    private string _motionFilterRollLimitMax = string.Empty;
    private string _motionFilterPitchSmoothing = string.Empty;
    private bool _motionFilterPitchLevel;
    private bool _motionFilterPitchLimit;
    private string _motionFilterPitchLimitMin = string.Empty;
    private string _motionFilterPitchLimitMax = string.Empty;
    private string _motionFilterYawSmoothing = string.Empty;
    private bool _motionFilterYawLevel;
    private bool _motionFilterYawLimit;
    private string _motionFilterYawLimitMin = string.Empty;
    private string _motionFilterYawLimitMax = string.Empty;
    // "Audio Source" section (audio-track item 10): the audio-renderable asset-graph
    // node this node references + per-entity play policy (auto_play / enabled). The
    // picked reference stores the STABLE asset-graph node id; the section shows when
    // the node HAS an audio_source component (added/removed via Add-Component / ✕).
    private InspectorAssetGraphRefOptionViewModel? _selectedAudioRenderableOption;
    private string _audioRenderableReferenceLabel = string.Empty;
    private bool _hasAudioSourceComponent;
    private bool _audioSourceAutoPlay = true;
    private bool _audioSourceEnabled = true;
    private string _componentsHeader = "Components";
    private bool _hasTransform;
    private string _translationX = string.Empty;
    private string _translationY = string.Empty;
    private string _translationZ = string.Empty;
    private string _rotationX = string.Empty;
    private string _rotationY = string.Empty;
    private string _rotationZ = string.Empty;
    private string _scaleX = string.Empty;
    private string _scaleY = string.Empty;
    private string _scaleZ = string.Empty;
    private bool _hasCameraComponent;
    private string _cameraFovY = string.Empty;
    private string _cameraNear = string.Empty;
    private string _cameraFar = string.Empty;
    private string _cameraAspect = string.Empty;
    private ulong _assetGraphNodeIdValue;
    private string _assetGraphNodeId = string.Empty;
    private string _assetGraphNodeName = string.Empty;
    private string _assetGraphNodeType = string.Empty;
    private string _assetGraphNodeTypeId = string.Empty;
    private string _assetGraphNodeSchema = string.Empty;
    private string _assetGraphNodeCompileStatus = string.Empty;
    private string _assetGraphNodePosition = string.Empty;
    // "GLB node" tree picker for the "Mesh from GLB scene" asset-graph node (issue
    // #213): the connected GLB scene's component hierarchy, shown as an expandable
    // tree so a node id can be picked instead of typed. Threaded asset-graph
    // topology (the snapshot's nodes + edges) lets the inspector walk the selected
    // extractor node's `scene` edge to the Scene-from-GLB node and that node's
    // `source_file` edge to the GLB file node to recover its source_path.
    private IReadOnlyList<EngineAssetGraphNode> _assetGraphNodes = [];
    private IReadOnlyList<EngineAssetGraphEdge> _assetGraphEdges = [];
    private bool _hasGlbNodePicker;
    private string _glbNodePickerHint = string.Empty;
    private string _lastEditError = string.Empty;
    private string _newBehaviorModule = string.Empty;
    // While true, populating fields from a selected node must not echo back to
    // the engine as a live edit.
    private bool _suppressLiveEdits;
    private SceneTreeNodeViewModel? _inspectedSceneNode;
    private AssetGraphSubGraph? _inspectedSubGraph;
    private string _subGraphName = string.Empty;
    private string _subGraphMemberCount = string.Empty;
    private AssetGraphRerouteModel? _reroutes;
    private string _nodeRerouteName = string.Empty;

    public InspectorPaneViewModel(
        IWozzitsEngineEditorSession? editorSession = null,
        Action<string>? log = null)
    {
        _editorSession = editorSession;
        _log = log;
        ApplyCameraCommand = new RelayCommand(
            ApplyCamera,
            () => HasSceneNodeSelection && HasCameraComponent);
        AddBehaviorCommand = new RelayCommand(
            AddBehavior,
            () => HasSceneNodeSelection);
        // These are only shown inside a selected scene node's inspector, so they
        // are always enabled; each guards with EnsureCanApply internally.
        AddComponentCommand = new RelayCommand<string>(AddComponent);
        RemoveCameraCommand = new RelayCommand(RemoveCameraComponent);
        ApplyRenderableCommand = new RelayCommand(ApplyRenderable);
        RemoveRenderableCommand = new RelayCommand(RemoveRenderable);
        // "Subtree from asset" (issue #213 piece 2): pick a "Scene from GLB" node to
        // reference, or clear it. Apply is gated on having a selection in the picker.
        RemoveSubtreeComponentCommand = new RelayCommand(RemoveSubtreeComponent);
        // "Render program" (issue #213): pick a render-program node; the header ✕
        // clears it and hides the section, mirroring the camera.
        RemoveRenderProgramComponentCommand = new RelayCommand(RemoveRenderProgramComponent);
        RemoveGeometryComponentCommand = new RelayCommand(RemoveGeometryComponent);
        // "Collision" / "Motion" (terrain-stick track): the header ✕ removes the
        // component via the generic remove verb and hides the section, like camera.
        RemoveCollisionComponentCommand = new RelayCommand(RemoveCollisionComponent);
        RemoveMotionComponentCommand = new RelayCommand(RemoveMotionComponent);
        RemoveAtmosphereComponentCommand = new RelayCommand(RemoveAtmosphereComponent);
        RemoveEnvironmentComponentCommand =
            new RelayCommand(RemoveEnvironmentComponent);
        RemoveMotionFilterComponentCommand =
            new RelayCommand(RemoveMotionFilterComponent);
        // "Audio Source" (audio-track item 10): the header ✕ removes the component
        // via the generic remove verb and hides the section, like collision.
        RemoveAudioSourceComponentCommand =
            new RelayCommand(RemoveAudioSourceComponent);
        // "Statechart runner": attach an authored chart to the selected node so it runs on Play.
        // Always enabled; guards with EnsureCanApply + a picked chart internally.
        AttachStatechartRunnerCommand = new RelayCommand(AttachStatechartRunner);
        RemoveStatechartRunnerCommand = new RelayCommand(RemoveStatechartRunner);
        // "Mind": point a node's quantum_agent at an authored mind graph (embeds mind_ir).
        AttachMindCommand = new RelayCommand(AttachMind);
        DetachMindCommand = new RelayCommand(DetachMind);
    }

    // Raised after a scene-source reference/descriptor was set or cleared on the
    // selected node (issue #213): the runtime's grafted children changed, so the
    // host (MainWindowViewModel) re-merges them into the scene tree.
    public event Action? SceneSourceChanged;

    // Raised after an asset-graph node param is applied to the engine (#218
    // Phase 3). The edit lands in the engine draft, but the asset-graph pane's
    // cached node cards still hold their pre-edit params; without re-pulling the
    // graph, re-selecting the node re-inspects the stale card and the value
    // appears to "reset" to its default. The host (MainWindowViewModel) handles
    // this by reloading the graph from the live session.
    public event Action? AssetGraphNodeParamApplied;

    // Raised after a named reroute is created/renamed/removed on the selected asset-graph
    // node, so the host re-projects the graph panes (issue woguls/wozzits-editor#1).
    public event Action? RerouteChanged;

    public ObservableCollection<InspectorComponentViewModel> Components { get; } = [];

    public ObservableCollection<InspectorBehaviorViewModel> Behaviors { get; } = [];

    // The "Scene from GLB" asset-graph nodes the "Subtree from asset" picker offers
    // (issue #213 piece 2). Threaded in from MainWindowViewModel whenever a node is
    // inspected (a snapshot-time list; live graph-edit refresh is out of scope).
    public ObservableCollection<InspectorSceneSourceOptionViewModel>
        AvailableSceneSources { get; } = [];

    // The render-program asset-graph nodes the "Render program" picker offers
    // (issue #213). Threaded in from MainWindowViewModel on every selection,
    // filtered to RenderProgram outputs.
    public ObservableCollection<InspectorAssetGraphRefOptionViewModel>
        AvailableRenderPrograms { get; } = [];

    // The mesh-producing asset-graph nodes the "Geometry" picker offers (issue
    // #213 increment 2). Threaded in the same way, filtered to Mesh outputs.
    public ObservableCollection<InspectorAssetGraphRefOptionViewModel>
        AvailableGeometrySources { get; } = [];

    // The Collision asset-graph nodes the "Collision" picker offers (terrain-stick
    // track). Threaded in from MainWindowViewModel on every selection, filtered to
    // Collision outputs (kAssetTypeCollisionAsset = 150).
    public ObservableCollection<InspectorAssetGraphRefOptionViewModel>
        AvailableCollisionSources { get; } = [];

    // The Atmosphere asset-graph nodes the "Atmosphere" picker offers. Threaded in
    // from MainWindowViewModel on every selection, filtered to Atmosphere outputs
    // (kAssetTypeAtmosphere = 2289).
    public ObservableCollection<InspectorAssetGraphRefOptionViewModel>
        AvailableAtmospheres { get; } = [];

    // The FrameEnvironment asset-graph nodes the "Environment" picker offers.
    // Threaded in from MainWindowViewModel on every selection, filtered to
    // FrameEnvironment outputs (kAssetTypeFrameEnvironment = 2290).
    public ObservableCollection<InspectorAssetGraphRefOptionViewModel>
        AvailableEnvironments { get; } = [];

    // The audio-renderable asset-graph nodes the "Audio Source" picker offers
    // (audio-track item 10). Threaded in from MainWindowViewModel on every
    // selection, filtered to audio-renderable outputs (kAssetTypeAudioRenderable
    // = 2142).
    public ObservableCollection<InspectorAssetGraphRefOptionViewModel>
        AvailableAudioRenderables { get; } = [];

    // Registered behavior modules offered by the "+" add menu, refreshed from the
    // running engine each time a scene node is inspected. Each option carries its
    // own add command so the flyout binds without reaching out of the popup.
    public ObservableCollection<InspectorBehaviorModuleOptionViewModel>
        AvailableBehaviorModules { get; } = [];

    public ObservableCollection<InspectorAssetGraphPortViewModel> AssetGraphInputPorts { get; } = [];

    public ObservableCollection<InspectorAssetGraphPortViewModel> AssetGraphOutputPorts { get; } = [];

    public ObservableCollection<InspectorAssetGraphDiagnosticViewModel> AssetGraphDiagnostics { get; } = [];

    public ObservableCollection<InspectorAssetGraphParamViewModel> AssetGraphParams { get; } = [];

    // Root nodes of the "GLB node" tree picker shown for the "Mesh from GLB scene"
    // asset-graph node (issue #213). Each node may have Children; mesh-bearing nodes
    // are pickable and set the node's `node_id` param. Empty unless that node is
    // selected AND its connected GLB hierarchy resolved.
    public ObservableCollection<InspectorGlbComponentNodeViewModel> GlbNodes { get; } = [];

    public IRelayCommand ApplyCameraCommand { get; }

    public IRelayCommand AddBehaviorCommand { get; }

    public IRelayCommand<string> AddComponentCommand { get; }

    public IRelayCommand RemoveCameraCommand { get; }

    public IRelayCommand ApplyRenderableCommand { get; }

    public IRelayCommand RemoveRenderableCommand { get; }

    // "Subtree from asset" (issue #213 piece 2).
    public IRelayCommand RemoveSubtreeComponentCommand { get; }

    // "Render program" (issue #213).
    public IRelayCommand RemoveRenderProgramComponentCommand { get; }

    public IRelayCommand RemoveGeometryComponentCommand { get; }

    // "Collision" / "Motion" (terrain-stick track).
    public IRelayCommand RemoveCollisionComponentCommand { get; }

    public IRelayCommand RemoveMotionComponentCommand { get; }

    // "Atmosphere" section header ✕.
    public IRelayCommand RemoveAtmosphereComponentCommand { get; }

    // "Environment" section header ✕.
    public IRelayCommand RemoveEnvironmentComponentCommand { get; }

    public IRelayCommand RemoveMotionFilterComponentCommand { get; }

    // "Audio Source" (audio-track item 10).
    public IRelayCommand RemoveAudioSourceComponentCommand { get; }

    public string NewBehaviorModule
    {
        get => _newBehaviorModule;
        set => SetProperty(ref _newBehaviorModule, value);
    }

    public string EmptyState
    {
        get => _emptyState;
        private set => SetProperty(ref _emptyState, value);
    }

    public string Header
    {
        get => _header;
        private set => SetProperty(ref _header, value);
    }

    public bool HasSelection => _selectionKind != InspectorSelectionKind.None;

    public bool HasNoSelection => !HasSelection;

    public bool HasSceneNodeSelection => _selectionKind == InspectorSelectionKind.SceneNode;

    public bool HasAssetGraphNodeSelection => _selectionKind == InspectorSelectionKind.AssetGraphNode;

    public bool HasSubGraphSelection => _selectionKind == InspectorSelectionKind.SubGraph;

    public bool HasStatechartNodeSelection => _selectionKind == InspectorSelectionKind.StatechartDataflowNode;

    public bool HasStatechartStateSelection => _selectionKind == InspectorSelectionKind.StatechartState;

    public DataflowNodeViewModel? SelectedStatechartNode
    {
        get => _selectedStatechartNode;
        private set => SetProperty(ref _selectedStatechartNode, value);
    }

    public StateNodeViewModel? SelectedStatechartState
    {
        get => _selectedStatechartState;
        private set => SetProperty(ref _selectedStatechartState, value);
    }

    // The selected sub-graph's editable name (issue woguls/wozzits-editor#1). Edits rename
    // it live — the proxy card binds Name — and persist via the sidecar on Save All. An
    // empty/whitespace value is ignored so a mid-edit clear doesn't blank the group.
    public string SubGraphName
    {
        get => _subGraphName;
        set
        {
            if (SetProperty(ref _subGraphName, value) && !_suppressLiveEdits)
            {
                OnSubGraphNameEdited();
            }
        }
    }

    public string SubGraphMemberCount
    {
        get => _subGraphMemberCount;
        private set => SetProperty(ref _subGraphMemberCount, value);
    }

    // Scene-node edits run against the live viewport runtime, so the whole
    // scene-node edit surface is disabled when it is down (mirrors the scene
    // tree's CanEditScene; the view binds the section's IsEnabled to this).
    // Evaluated live and re-notified on selection (Inspect) and after a viewport
    // restart; command edits also re-check via EnsureCanApply, so an edit
    // attempted after the viewport stopped mid-session is still logged + skipped.
    public bool CanEditNode => _editorSession?.IsRuntimeRunning ?? false;

    // Re-raise CanEditNode so the bound edit surface re-evaluates (there is no
    // push notification when the runtime starts/stops). Called from Inspect and
    // by the host after Restart Viewport.
    public void RefreshEditAvailability() => OnPropertyChanged(nameof(CanEditNode));

    public string NodeId
    {
        get => _nodeId;
        private set => SetProperty(ref _nodeId, value);
    }

    public string NodeName
    {
        get => _nodeName;
        set { if (SetProperty(ref _nodeName, value)) OnNodePropertiesEdited(); }
    }

    public string ParentId
    {
        get => _parentId;
        private set => SetProperty(ref _parentId, value);
    }

    public bool NodeVisible
    {
        get => _nodeVisible;
        set { if (SetProperty(ref _nodeVisible, value)) OnNodePropertiesEdited(); }
    }

    // Draw-order LAYER options for the node's render_order dropdown. Values mirror
    // the engine's render_layer constants; lower draws first. This is the cross-
    // cutting layer override — within a layer, draw order is the tree / reorder.
    public IReadOnlyList<InspectorRenderLayerOption> RenderLayers { get; } =
    [
        new InspectorRenderLayerOption("Background (Sky)", -200),
        new InspectorRenderLayerOption("World", 0),
        new InspectorRenderLayerOption("Transparent", 100),
        new InspectorRenderLayerOption("Overlay", 200),
    ];

    // The selected node's current layer (the option whose value == its
    // render_order, or null when the node's value is non-standard). Setting it
    // pushes the layer live to the running engine, which re-bakes draw order.
    public InspectorRenderLayerOption? SelectedRenderLayer
    {
        get => _selectedRenderLayer;
        set { if (SetProperty(ref _selectedRenderLayer, value)) OnRenderLayerEdited(); }
    }

    private InspectorRenderLayerOption? RenderLayerFor(int renderOrder)
    {
        foreach (var option in RenderLayers)
        {
            if (option.Value == renderOrder)
            {
                return option;
            }
        }
        return null;  // non-standard value => no named layer selected
    }

    public string RenderableSource
    {
        get => _renderableSource;
        private set => SetProperty(ref _renderableSource, value);
    }

    public string RenderableSourceKind
    {
        get => _renderableSourceKind;
        private set => SetProperty(ref _renderableSourceKind, value);
    }

    public string RenderableAssetGraphNodeId
    {
        get => _renderableAssetGraphNodeId;
        set => SetProperty(ref _renderableAssetGraphNodeId, value);
    }

    public bool HasRenderableReference
    {
        get => _hasRenderableReference;
        private set => SetProperty(ref _hasRenderableReference, value);
    }

    // The "Scene from GLB" node currently chosen in the "Subtree from asset" picker
    // (issue #213 piece 2). Bound TwoWay to the ComboBox; gates Apply's can-execute.
    public InspectorSceneSourceOptionViewModel? SelectedSceneSourceOption
    {
        get => _selectedSceneSourceOption;
        set
        {
            // A user pick applies immediately (like editing a component field in the
            // inspector): reference + graft the chosen Scene-from-GLB subtree, no
            // separate Apply button. Programmatic restores (SetAvailableSceneSources)
            // assign the field, not this setter, so they never re-apply; clearing sets
            // null, which is skipped.
            if (SetProperty(ref _selectedSceneSourceOption, value)
                && value is not null)
            {
                ApplySceneSource();
            }
        }
    }

    public bool HasAvailableSceneSources => AvailableSceneSources.Count > 0;

    // Optimistic, session-local "Referencing: <node>" line for the chosen subtree
    // source (issue #213 piece 2). Empty => "(none)" (no reference picked this
    // session). Reading the persisted reference back from the snapshot is piece 3.
    public string SubtreeReferenceLabel
    {
        get => _subtreeReferenceLabel;
        private set
        {
            if (SetProperty(ref _subtreeReferenceLabel, value))
            {
                OnPropertyChanged(nameof(HasSubtreeReference));
                OnPropertyChanged(nameof(SubtreeReferenceDisplay));
            }
        }
    }

    public bool HasSubtreeReference =>
        !string.IsNullOrWhiteSpace(SubtreeReferenceLabel);

    public string SubtreeReferenceDisplay =>
        HasSubtreeReference ? $"Referencing: {SubtreeReferenceLabel}" : "(none)";

    // Gates the "Subtree from asset" picker section, mirroring HasRenderableReference
    // (issue #213 piece 2 review): the section is attached via "Add Component →
    // subtree_from_asset" rather than always shown. Like renderable, attaching it
    // does NOT call the generic AddNodeComponent verb (the engine rejects it) — it
    // just reveals the existing picker so the user references a Scene-from-GLB node.
    public bool HasSubtreeSection
    {
        get => _hasSubtreeSection;
        private set => SetProperty(ref _hasSubtreeSection, value);
    }

    // ─── Render program (issue #213) ─────────────────────────────────────────────

    // The render-program node chosen in the "Render program" picker. Bound TwoWay to
    // the ComboBox; a user pick applies immediately (no Apply button), mirroring the
    // subtree picker. The program cascades to descendants without their own.
    // Programmatic restores assign the field, not this setter.
    public InspectorAssetGraphRefOptionViewModel? SelectedRenderProgramOption
    {
        get => _selectedRenderProgramOption;
        set
        {
            if (SetProperty(ref _selectedRenderProgramOption, value)
                && value is not null)
            {
                ApplyRenderProgram();
            }
        }
    }

    public bool HasAvailableRenderPrograms => AvailableRenderPrograms.Count > 0;

    // Optimistic "Referencing: <node>" line for the picked program. Empty =>
    // "(none)".
    public string RenderProgramReferenceLabel
    {
        get => _renderProgramReferenceLabel;
        private set
        {
            if (SetProperty(ref _renderProgramReferenceLabel, value))
            {
                OnPropertyChanged(nameof(RenderProgramReferenceDisplay));
            }
        }
    }

    public string RenderProgramReferenceDisplay =>
        string.IsNullOrWhiteSpace(RenderProgramReferenceLabel)
            ? "(none)"
            : $"Referencing: {RenderProgramReferenceLabel}";

    // Gates the "Render program" section, mirroring HasSubtreeSection: attached via
    // "Add Component → render_program" rather than always shown. Attaching it does
    // NOT call the generic AddNodeComponent verb (the engine rejects it) — it just
    // reveals the picker so the user references a render-program node.
    public bool HasRenderProgramSection
    {
        get => _hasRenderProgramSection;
        private set => SetProperty(ref _hasRenderProgramSection, value);
    }

    // ─── Geometry (issue #213 increment 2) ───────────────────────────────────────

    // The mesh node chosen in the "Geometry" picker: the GEOMETRY half of the same
    // renderable binding the render-program section authors, so a node can pair a
    // mesh with a program directly — no GLB subtree graft required. A user pick
    // applies immediately (no Apply button), mirroring the program picker.
    // Programmatic restores assign the field, not this setter.
    public InspectorAssetGraphRefOptionViewModel? SelectedGeometryOption
    {
        get => _selectedGeometryOption;
        set
        {
            if (SetProperty(ref _selectedGeometryOption, value)
                && value is not null)
            {
                ApplyGeometry();
            }
        }
    }

    public bool HasAvailableGeometrySources => AvailableGeometrySources.Count > 0;

    // Optimistic "Referencing: <node>" line for the picked mesh. Empty => "(none)".
    public string GeometryReferenceLabel
    {
        get => _geometryReferenceLabel;
        private set
        {
            if (SetProperty(ref _geometryReferenceLabel, value))
            {
                OnPropertyChanged(nameof(GeometryReferenceDisplay));
            }
        }
    }

    public string GeometryReferenceDisplay =>
        string.IsNullOrWhiteSpace(GeometryReferenceLabel)
            ? "(none)"
            : $"Referencing: {GeometryReferenceLabel}";

    // Gates the "Geometry" section, mirroring HasRenderProgramSection: attached via
    // "Add Component → Geometry (mesh)" rather than always shown. Attaching does NOT
    // call the generic AddNodeComponent verb (the engine rejects "geometry") — it
    // just reveals the picker so the user references a mesh-producing node.
    public bool HasGeometrySection
    {
        get => _hasGeometrySection;
        private set => SetProperty(ref _hasGeometrySection, value);
    }

    // ── Renderable bindings + constants (issue #230) ────────────────────────
    // The custom-renderable ingredient form, GENERATED from the effective
    // render program's authored binding layout: one source picker per declared
    // SRV semantic (options filtered by the row's resource kind) and one typed
    // value editor per declared constant tail field. Revealed when the layout
    // resolves (or the node already carries ingredients — then with a hint).

    public ObservableCollection<InspectorRenderableBindingRowViewModel>
        RenderableBindingRows { get; } = [];

    public ObservableCollection<InspectorRenderableConstantRowViewModel>
        RenderableConstantRows { get; } = [];

    public bool HasRenderableIngredientsSection
    {
        get => _hasRenderableIngredientsSection;
        private set => SetProperty(ref _hasRenderableIngredientsSection, value);
    }

    public string RenderableIngredientsHint
    {
        get => _renderableIngredientsHint;
        private set
        {
            if (SetProperty(ref _renderableIngredientsHint, value))
            {
                OnPropertyChanged(nameof(HasRenderableIngredientsHint));
            }
        }
    }

    public bool HasRenderableIngredientsHint =>
        !string.IsNullOrWhiteSpace(RenderableIngredientsHint);

    public bool HasRenderableBindingRows => RenderableBindingRows.Count > 0;

    public bool HasRenderableConstantRows => RenderableConstantRows.Count > 0;

    // Cross-pane scene-node lookup (wired by the main VM to the scene tree):
    // the effective-render-program resolution walks ParentId ancestors, and
    // the inspector holds only the selected node.
    public void SetSceneNodeLookup(Func<string, SceneTreeNodeViewModel?> lookup)
    {
        _sceneNodeLookup = lookup;
    }

    // The shared named-reroute model, so the inspector can name a selected node's output.
    public void SetRerouteModel(AssetGraphRerouteModel reroutes)
    {
        _reroutes = reroutes;
    }

    // ─── Collision (terrain-stick track) ─────────────────────────────────────────

    // The Collision asset-graph node chosen in the picker. Bound TwoWay to the
    // ComboBox; a user pick applies immediately (no Apply button), mirroring the
    // render-program picker. Programmatic restores assign the field, not this setter.
    public InspectorAssetGraphRefOptionViewModel? SelectedCollisionOption
    {
        get => _selectedCollisionOption;
        set
        {
            if (SetProperty(ref _selectedCollisionOption, value)
                && value is not null)
            {
                ApplyCollision();
            }
        }
    }

    public bool HasAvailableCollisionSources => AvailableCollisionSources.Count > 0;

    // Optimistic "Referencing: <node>" line for the picked collision asset. Empty
    // => "(none)". Reading the persisted reference back from the snapshot is a
    // deferred follow-up (the engine does not yet surface it), exactly like the
    // render-program picker shipped optimistic first.
    public string CollisionReferenceLabel
    {
        get => _collisionReferenceLabel;
        private set
        {
            if (SetProperty(ref _collisionReferenceLabel, value))
            {
                OnPropertyChanged(nameof(CollisionReferenceDisplay));
            }
        }
    }

    public string CollisionReferenceDisplay =>
        string.IsNullOrWhiteSpace(CollisionReferenceLabel)
            ? "(none)"
            : $"Referencing: {CollisionReferenceLabel}";

    // Gates the "Collision" section: shown when the node HAS a collision component
    // (added via Add-Component → Collision, removed via the section's ✕).
    public bool HasCollisionComponent
    {
        get => _hasCollisionComponent;
        private set => SetProperty(ref _hasCollisionComponent, value);
    }

    // Whether the node's movement is constrained by the referenced collision data.
    // Toggling re-applies SetNodeCollision with the current selection (or 0).
    public bool CollisionConstrainMovement
    {
        get => _collisionConstrainMovement;
        set
        {
            if (SetProperty(ref _collisionConstrainMovement, value))
            {
                OnCollisionFieldEdited();
            }
        }
    }

    // ─── Atmosphere ──────────────────────────────────────────────────────────────

    // The Atmosphere asset-graph node chosen in the picker. Bound TwoWay to the
    // ComboBox; a pick applies immediately (no Apply button). Programmatic restores
    // assign the field, not this setter.
    public InspectorAssetGraphRefOptionViewModel? SelectedAtmosphereOption
    {
        get => _selectedAtmosphereOption;
        set
        {
            if (SetProperty(ref _selectedAtmosphereOption, value)
                && value is not null)
            {
                ApplyAtmosphere();
            }
        }
    }

    public bool HasAvailableAtmospheres => AvailableAtmospheres.Count > 0;

    // "Referencing: <node>" line for the picked Atmosphere asset. Empty => "(none)".
    public string AtmosphereReferenceLabel
    {
        get => _atmosphereReferenceLabel;
        private set
        {
            if (SetProperty(ref _atmosphereReferenceLabel, value))
            {
                OnPropertyChanged(nameof(AtmosphereReferenceDisplay));
            }
        }
    }

    public string AtmosphereReferenceDisplay =>
        string.IsNullOrWhiteSpace(AtmosphereReferenceLabel)
            ? "(none)"
            : $"Referencing: {AtmosphereReferenceLabel}";

    // Gates the "Atmosphere" section: shown when the node HAS an atmosphere
    // component (added via Add-Component → Atmosphere, removed via the section's ✕).
    public bool HasAtmosphereComponent
    {
        get => _hasAtmosphereComponent;
        private set => SetProperty(ref _hasAtmosphereComponent, value);
    }

    // The master switch for this atmosphere binding. Toggling re-applies
    // SetNodeAtmosphere with the current selection (or 0).
    public bool AtmosphereEnabled
    {
        get => _atmosphereEnabled;
        set
        {
            if (SetProperty(ref _atmosphereEnabled, value))
            {
                OnAtmosphereFieldEdited();
            }
        }
    }

    // ─── Environment ─────────────────────────────────────────────────────────────

    // The FrameEnvironment asset-graph node chosen in the picker. Bound TwoWay to
    // the ComboBox; a pick applies immediately. Programmatic restores assign the
    // field, not this setter.
    public InspectorAssetGraphRefOptionViewModel? SelectedEnvironmentOption
    {
        get => _selectedEnvironmentOption;
        set
        {
            if (SetProperty(ref _selectedEnvironmentOption, value)
                && value is not null)
            {
                ApplyEnvironment();
            }
        }
    }

    public bool HasAvailableEnvironments => AvailableEnvironments.Count > 0;

    // "Referencing: <node>" line for the picked FrameEnvironment asset.
    // Empty => "(none)".
    public string EnvironmentReferenceLabel
    {
        get => _environmentReferenceLabel;
        private set
        {
            if (SetProperty(ref _environmentReferenceLabel, value))
            {
                OnPropertyChanged(nameof(EnvironmentReferenceDisplay));
            }
        }
    }

    public string EnvironmentReferenceDisplay =>
        string.IsNullOrWhiteSpace(EnvironmentReferenceLabel)
            ? "(none)"
            : $"Referencing: {EnvironmentReferenceLabel}";

    // Gates the "Environment" section: shown when the node HAS an environment
    // component (added via Add-Component → Environment, removed via the ✕).
    public bool HasEnvironmentComponent
    {
        get => _hasEnvironmentComponent;
        private set => SetProperty(ref _hasEnvironmentComponent, value);
    }

    // The master switch for this environment binding. Toggling re-applies
    // SetNodeEnvironment with the current selection (or 0).
    public bool EnvironmentEnabled
    {
        get => _environmentEnabled;
        set
        {
            if (SetProperty(ref _environmentEnabled, value))
            {
                OnEnvironmentFieldEdited();
            }
        }
    }

    // ─── Audio Source (audio-track item 10) ──────────────────────────────────────

    // The audio-renderable asset-graph node chosen in the picker. Bound TwoWay to
    // the ComboBox; a user pick applies immediately, mirroring the collision picker.
    public InspectorAssetGraphRefOptionViewModel? SelectedAudioRenderableOption
    {
        get => _selectedAudioRenderableOption;
        set
        {
            if (SetProperty(ref _selectedAudioRenderableOption, value)
                && value is not null)
            {
                ApplyAudioRenderable();
            }
        }
    }

    public bool HasAvailableAudioRenderables =>
        AvailableAudioRenderables.Count > 0;

    public string AudioRenderableReferenceLabel
    {
        get => _audioRenderableReferenceLabel;
        private set
        {
            if (SetProperty(ref _audioRenderableReferenceLabel, value))
            {
                OnPropertyChanged(nameof(AudioRenderableReferenceDisplay));
            }
        }
    }

    public string AudioRenderableReferenceDisplay =>
        string.IsNullOrWhiteSpace(AudioRenderableReferenceLabel)
            ? "(none)"
            : $"Referencing: {AudioRenderableReferenceLabel}";

    // Gates the "Audio Source" section: shown when the node HAS an audio_source
    // component (added via Add-Component → Audio Source, removed via the ✕).
    public bool HasAudioSourceComponent
    {
        get => _hasAudioSourceComponent;
        private set => SetProperty(ref _hasAudioSourceComponent, value);
    }

    // Per-entity play policy. Toggling re-applies SetNodeAudioSourcePlay.
    public bool AudioSourceAutoPlay
    {
        get => _audioSourceAutoPlay;
        set
        {
            if (SetProperty(ref _audioSourceAutoPlay, value))
            {
                OnAudioSourcePlayEdited();
            }
        }
    }

    public bool AudioSourceEnabled
    {
        get => _audioSourceEnabled;
        set
        {
            if (SetProperty(ref _audioSourceEnabled, value))
            {
                OnAudioSourcePlayEdited();
            }
        }
    }

    // ─── Motion (terrain-stick track) ────────────────────────────────────────────

    // Gates the "Motion" section: shown when the node HAS a motion component.
    public bool HasMotionComponent
    {
        get => _hasMotionComponent;
        private set => SetProperty(ref _hasMotionComponent, value);
    }

    public bool MotionTerrainConstrained
    {
        get => _motionTerrainConstrained;
        set { if (SetProperty(ref _motionTerrainConstrained, value)) OnMotionFieldEdited(); }
    }

    public string MotionRideHeight
    {
        get => _motionRideHeight;
        set { if (SetProperty(ref _motionRideHeight, value)) OnMotionFieldEdited(); }
    }

    public string MotionFootprintRadius
    {
        get => _motionFootprintRadius;
        set { if (SetProperty(ref _motionFootprintRadius, value)) OnMotionFieldEdited(); }
    }

    public bool MotionAlignToSurface
    {
        get => _motionAlignToSurface;
        set { if (SetProperty(ref _motionAlignToSurface, value)) OnMotionFieldEdited(); }
    }

    public string MotionAlignmentStrength
    {
        get => _motionAlignmentStrength;
        set { if (SetProperty(ref _motionAlignmentStrength, value)) OnMotionFieldEdited(); }
    }

    // ─── Motion Filter (secondary-motion camera damping) ─────────────────────────

    // Gates the "Motion Filter" section: shown when the node HAS the component.
    public bool HasMotionFilterComponent
    {
        get => _hasMotionFilterComponent;
        private set => SetProperty(ref _hasMotionFilterComponent, value);
    }

    public bool MotionFilterEnabled
    {
        get => _motionFilterEnabled;
        set { if (SetProperty(ref _motionFilterEnabled, value)) OnMotionFilterFieldEdited(); }
    }

    // Translation smoothing per world axis (seconds; 0 = pass through).
    public string MotionFilterTranslationSmoothingX
    {
        get => _motionFilterTranslationSmoothingX;
        set { if (SetProperty(ref _motionFilterTranslationSmoothingX, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterTranslationSmoothingY
    {
        get => _motionFilterTranslationSmoothingY;
        set { if (SetProperty(ref _motionFilterTranslationSmoothingY, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterTranslationSmoothingZ
    {
        get => _motionFilterTranslationSmoothingZ;
        set { if (SetProperty(ref _motionFilterTranslationSmoothingZ, value)) OnMotionFilterFieldEdited(); }
    }

    public bool MotionFilterTerrainFloor
    {
        get => _motionFilterTerrainFloor;
        set { if (SetProperty(ref _motionFilterTerrainFloor, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterTerrainFloorOffset
    {
        get => _motionFilterTerrainFloorOffset;
        set { if (SetProperty(ref _motionFilterTerrainFloorOffset, value)) OnMotionFilterFieldEdited(); }
    }

    // Rotation channels (node-local roll/pitch/yaw).
    public string MotionFilterRollSmoothing
    {
        get => _motionFilterRollSmoothing;
        set { if (SetProperty(ref _motionFilterRollSmoothing, value)) OnMotionFilterFieldEdited(); }
    }

    public bool MotionFilterRollLevel
    {
        get => _motionFilterRollLevel;
        set { if (SetProperty(ref _motionFilterRollLevel, value)) OnMotionFilterFieldEdited(); }
    }

    public bool MotionFilterRollLimit
    {
        get => _motionFilterRollLimit;
        set { if (SetProperty(ref _motionFilterRollLimit, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterRollLimitMin
    {
        get => _motionFilterRollLimitMin;
        set { if (SetProperty(ref _motionFilterRollLimitMin, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterRollLimitMax
    {
        get => _motionFilterRollLimitMax;
        set { if (SetProperty(ref _motionFilterRollLimitMax, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterPitchSmoothing
    {
        get => _motionFilterPitchSmoothing;
        set { if (SetProperty(ref _motionFilterPitchSmoothing, value)) OnMotionFilterFieldEdited(); }
    }

    public bool MotionFilterPitchLevel
    {
        get => _motionFilterPitchLevel;
        set { if (SetProperty(ref _motionFilterPitchLevel, value)) OnMotionFilterFieldEdited(); }
    }

    public bool MotionFilterPitchLimit
    {
        get => _motionFilterPitchLimit;
        set { if (SetProperty(ref _motionFilterPitchLimit, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterPitchLimitMin
    {
        get => _motionFilterPitchLimitMin;
        set { if (SetProperty(ref _motionFilterPitchLimitMin, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterPitchLimitMax
    {
        get => _motionFilterPitchLimitMax;
        set { if (SetProperty(ref _motionFilterPitchLimitMax, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterYawSmoothing
    {
        get => _motionFilterYawSmoothing;
        set { if (SetProperty(ref _motionFilterYawSmoothing, value)) OnMotionFilterFieldEdited(); }
    }

    public bool MotionFilterYawLevel
    {
        get => _motionFilterYawLevel;
        set { if (SetProperty(ref _motionFilterYawLevel, value)) OnMotionFilterFieldEdited(); }
    }

    public bool MotionFilterYawLimit
    {
        get => _motionFilterYawLimit;
        set { if (SetProperty(ref _motionFilterYawLimit, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterYawLimitMin
    {
        get => _motionFilterYawLimitMin;
        set { if (SetProperty(ref _motionFilterYawLimitMin, value)) OnMotionFilterFieldEdited(); }
    }

    public string MotionFilterYawLimitMax
    {
        get => _motionFilterYawLimitMax;
        set { if (SetProperty(ref _motionFilterYawLimitMax, value)) OnMotionFilterFieldEdited(); }
    }

    public bool HasTransform
    {
        get => _hasTransform;
        private set => SetProperty(ref _hasTransform, value);
    }

    public string TranslationX
    {
        get => _translationX;
        set { if (SetProperty(ref _translationX, value)) PushLiveTransform(); }
    }

    public string TranslationY
    {
        get => _translationY;
        set { if (SetProperty(ref _translationY, value)) PushLiveTransform(); }
    }

    public string TranslationZ
    {
        get => _translationZ;
        set { if (SetProperty(ref _translationZ, value)) PushLiveTransform(); }
    }

    public string RotationX
    {
        get => _rotationX;
        set { if (SetProperty(ref _rotationX, value)) PushLiveTransform(); }
    }

    public string RotationY
    {
        get => _rotationY;
        set { if (SetProperty(ref _rotationY, value)) PushLiveTransform(); }
    }

    public string RotationZ
    {
        get => _rotationZ;
        set { if (SetProperty(ref _rotationZ, value)) PushLiveTransform(); }
    }

    public string ScaleX
    {
        get => _scaleX;
        set { if (SetProperty(ref _scaleX, value)) PushLiveTransform(); }
    }

    public string ScaleY
    {
        get => _scaleY;
        set { if (SetProperty(ref _scaleY, value)) PushLiveTransform(); }
    }

    public string ScaleZ
    {
        get => _scaleZ;
        set { if (SetProperty(ref _scaleZ, value)) PushLiveTransform(); }
    }

    public bool HasComponents => Components.Count > 0;

    public bool HasNoComponents => !HasComponents;

    public bool HasBehaviors => Behaviors.Count > 0;

    public bool HasNoBehaviors => !HasBehaviors;

    public bool HasAvailableBehaviorModules => AvailableBehaviorModules.Count > 0;

    public string ComponentsHeader
    {
        get => _componentsHeader;
        private set => SetProperty(ref _componentsHeader, value);
    }

    public bool HasCameraComponent
    {
        get => _hasCameraComponent;
        private set
        {
            if (SetProperty(ref _hasCameraComponent, value))
            {
                ApplyCameraCommand.NotifyCanExecuteChanged();
            }
        }
    }

    public string CameraFovY
    {
        get => _cameraFovY;
        set => SetProperty(ref _cameraFovY, value);
    }

    public string CameraNear
    {
        get => _cameraNear;
        set => SetProperty(ref _cameraNear, value);
    }

    public string CameraFar
    {
        get => _cameraFar;
        set => SetProperty(ref _cameraFar, value);
    }

    public string CameraAspect
    {
        get => _cameraAspect;
        set => SetProperty(ref _cameraAspect, value);
    }

    public string AssetGraphNodeId
    {
        get => _assetGraphNodeId;
        private set => SetProperty(ref _assetGraphNodeId, value);
    }

    public string AssetGraphNodeName
    {
        get => _assetGraphNodeName;
        private set => SetProperty(ref _assetGraphNodeName, value);
    }

    public string AssetGraphNodeType
    {
        get => _assetGraphNodeType;
        private set => SetProperty(ref _assetGraphNodeType, value);
    }

    public string AssetGraphNodeTypeId
    {
        get => _assetGraphNodeTypeId;
        private set => SetProperty(ref _assetGraphNodeTypeId, value);
    }

    public string AssetGraphNodeSchema
    {
        get => _assetGraphNodeSchema;
        private set => SetProperty(ref _assetGraphNodeSchema, value);
    }

    public string AssetGraphNodeCompileStatus
    {
        get => _assetGraphNodeCompileStatus;
        private set => SetProperty(ref _assetGraphNodeCompileStatus, value);
    }

    public string AssetGraphNodePosition
    {
        get => _assetGraphNodePosition;
        private set => SetProperty(ref _assetGraphNodePosition, value);
    }

    // The selected node's named-reroute label. Typing a name collapses the node's output
    // fan-out into that named badge; clearing it removes the reroute. Editor-only display
    // (issue woguls/wozzits-editor#1); persists in the sidecar on Save All.
    public string NodeRerouteName
    {
        get => _nodeRerouteName;
        set
        {
            if (SetProperty(ref _nodeRerouteName, value))
            {
                OnNodeRerouteNameEdited();
            }
        }
    }

    private void OnNodeRerouteNameEdited()
    {
        if (_suppressLiveEdits || !HasAssetGraphNodeSelection || _reroutes is null)
        {
            return;
        }

        if (string.IsNullOrWhiteSpace(NodeRerouteName))
        {
            _reroutes.Remove(_assetGraphNodeIdValue);
        }
        else
        {
            _reroutes.Set(_assetGraphNodeIdValue, NodeRerouteName);
        }

        RerouteChanged?.Invoke();
    }

    public bool HasAssetGraphInputPorts => AssetGraphInputPorts.Count > 0;

    public bool HasNoAssetGraphInputPorts => !HasAssetGraphInputPorts;

    public bool HasAssetGraphOutputPorts => AssetGraphOutputPorts.Count > 0;

    public bool HasNoAssetGraphOutputPorts => !HasAssetGraphOutputPorts;

    public bool HasAssetGraphDiagnostics => AssetGraphDiagnostics.Count > 0;

    public bool HasNoAssetGraphDiagnostics => !HasAssetGraphDiagnostics;

    public bool HasAssetGraphParams => AssetGraphParams.Count > 0;

    public bool HasNoAssetGraphParams => !HasAssetGraphParams;

    // True only while a "Mesh from GLB scene" asset-graph node is selected: gates the
    // "GLB node" tree-picker section (issue #213). The generic `node_id` text param
    // stays editable as the fallback regardless of this.
    public bool HasGlbNodePicker
    {
        get => _hasGlbNodePicker;
        private set => SetProperty(ref _hasGlbNodePicker, value);
    }

    public bool HasGlbNodes => GlbNodes.Count > 0;

    // A short hint shown in the picker section when the GLB tree could not be built
    // (not connected to a Scene-from-GLB with a resolvable source_path, or the import
    // failed). Empty when the tree is present. The text param remains the fallback.
    public string GlbNodePickerHint
    {
        get => _glbNodePickerHint;
        private set
        {
            if (SetProperty(ref _glbNodePickerHint, value))
            {
                OnPropertyChanged(nameof(HasGlbNodePickerHint));
            }
        }
    }

    public bool HasGlbNodePickerHint => !string.IsNullOrWhiteSpace(GlbNodePickerHint);

    public string LastEditError
    {
        get => _lastEditError;
        private set
        {
            if (SetProperty(ref _lastEditError, value))
            {
                OnPropertyChanged(nameof(HasLastEditError));
            }
        }
    }

    public bool HasLastEditError => !string.IsNullOrWhiteSpace(LastEditError);

    public void Inspect(SceneTreeNodeViewModel? node)
    {
        Components.Clear();
        Behaviors.Clear();
        AvailableBehaviorModules.Clear();
        AssetGraphInputPorts.Clear();
        AssetGraphOutputPorts.Clear();
        AssetGraphDiagnostics.Clear();
        AssetGraphParams.Clear();
        ClearGlbNodePicker();
        LastEditError = string.Empty;
        // Re-evaluate whether editing is available for the freshly-selected node
        // (the viewport may have started/stopped since the last selection).
        RefreshEditAvailability();

        if (node is null)
        {
            Header = string.Empty;
            EmptyState = "No scene or asset graph node selected.";
            SetSelectionKind(InspectorSelectionKind.None);
            ClearNodeFields();
            ClearAssetGraphFields();
            NotifyComponentStateChanged();
            NotifyAssetGraphPortStateChanged();
            return;
        }

        Header = node.DisplayName;
        EmptyState = string.Empty;
        SetSelectionKind(InspectorSelectionKind.SceneNode);
        ClearAssetGraphFields();

        _inspectedSceneNode = node;
        _suppressLiveEdits = true;
        try
        {
            NodeId = node.Id;
            NodeName = node.DisplayName;
            ParentId = node.ParentId ?? string.Empty;
            NodeVisible = node.Visible ?? false;
            SelectedRenderLayer = RenderLayerFor(node.RenderOrder);

            HasRenderableReference = node.Renderable is not null;
            RenderableSource = node.RenderableSource.DisplayName;
            RenderableSourceKind = node.RenderableSource.Kind;
            RenderableAssetGraphNodeId =
                node.Renderable?.AssetGraphNodeId?.ToString(CultureInfo.InvariantCulture) ?? string.Empty;

            // The "Subtree from asset" and "Render program" sections are driven by
            // the node's persisted state (issue #213): reveal + pre-select them from
            // the authored asset-graph node ids surfaced in the snapshot, so a node
            // that has them shows them on (re)select instead of starting hidden.
            RestoreSubtreeReferenceState(node);
            RestoreRenderProgramState(node);
            RestoreGeometryState(node);
            RestoreRenderableIngredientsState(node);

            ComponentsHeader = $"{Header} Components";
            SetTransformFields(node.Transform);
            SetComponentFields(node);
        }
        finally
        {
            _suppressLiveEdits = false;
        }

        RefreshAvailableBehaviorModules();
        RefreshStatechartRunnerSection();
        RefreshQuantumAgentMindSection();
        NotifyComponentStateChanged();
        NotifyAssetGraphPortStateChanged();
    }

    // ---- Statechart runner: attach an authored chart to this scene node so it runs on Play -----
    // Added from the Components "+" menu (which reveals HasStatechartRunnerSection); the card then
    // offers just a chart picker -- the chart itself names the scene nodes its bindings resolve to,
    // so there is nothing else to wire here. The chart list is pulled fresh from a host provider.
    private Func<IReadOnlyList<StatechartFileInfo>>? _statechartsProvider;
    private StatechartFileInfo? _selectedRunnerChart;
    private string _statechartRunnerStatus = string.Empty;
    private bool _hasStatechartRunnerSection;

    public void SetStatechartsProvider(Func<IReadOnlyList<StatechartFileInfo>> provider) =>
        _statechartsProvider = provider;

    // Revealed by the Components "+" menu (AddComponent) for the selected node; reset on reselect.
    public bool HasStatechartRunnerSection
    {
        get => _hasStatechartRunnerSection;
        private set => SetProperty(ref _hasStatechartRunnerSection, value);
    }

    // The project's charts, offered in the runner card's picker (no typing).
    public ObservableCollection<StatechartFileInfo> StatechartRunnerCharts { get; } = [];

    public StatechartFileInfo? SelectedStatechartRunnerChart
    {
        get => _selectedRunnerChart;
        set
        {
            if (SetProperty(ref _selectedRunnerChart, value))
            {
                StatechartRunnerStatus = string.Empty;
                OnPropertyChanged(nameof(HasSelectedStatechartRunnerChart));
            }
        }
    }

    public bool HasSelectedStatechartRunnerChart => _selectedRunnerChart is not null;

    // A one-line result/hint under the attach button (e.g. "Attached — press Play", or why not).
    public string StatechartRunnerStatus
    {
        get => _statechartRunnerStatus;
        private set
        {
            if (SetProperty(ref _statechartRunnerStatus, value))
            {
                OnPropertyChanged(nameof(HasStatechartRunnerStatus));
            }
        }
    }

    public bool HasStatechartRunnerStatus => !string.IsNullOrEmpty(_statechartRunnerStatus);

    // True when the selected node already runs a chart -- the card then offers Remove and reflects
    // the running chart, so the runner lives only here (not as a raw Behaviors row).
    private bool _hasAttachedStatechartRunner;

    public bool HasAttachedStatechartRunner
    {
        get => _hasAttachedStatechartRunner;
        private set => SetProperty(ref _hasAttachedStatechartRunner, value);
    }

    public IRelayCommand AttachStatechartRunnerCommand { get; }

    public IRelayCommand RemoveStatechartRunnerCommand { get; }

    // Repopulate the picker for the freshly-selected node (called from Inspect). Charts are pulled
    // fresh so the list reflects the project regardless of whether the _Statecharts menu was opened.
    // The card auto-reveals when the node already runs a chart (else the Components "+" menu reveals
    // it), reflecting the running chart + offering Remove -- so the runner lives only here.
    private void RefreshStatechartRunnerSection()
    {
        SelectedStatechartRunnerChart = null;
        StatechartRunnerCharts.Clear();
        if (HasSceneNodeSelection && _statechartsProvider is not null)
        {
            foreach (var chart in _statechartsProvider())
            {
                StatechartRunnerCharts.Add(chart);
            }
        }

        var runner = _inspectedSceneNode?.Behaviors.FirstOrDefault(b => b.Module == "statechart_runner");
        HasAttachedStatechartRunner = runner is not null;
        HasStatechartRunnerSection = runner is not null;

        var current = runner?.Config.FirstOrDefault(c => c.Name == StatechartRunnerAttachment.ConfigChart)?.Value;
        if (string.IsNullOrEmpty(current))
        {
            StatechartRunnerStatus = string.Empty;
        }
        else
        {
            // Reflect the running chart IN THE PICKER (matched by name) so a reloaded runner shows
            // what it runs instead of looking unset. Set the field directly to keep the status.
            _selectedRunnerChart = StatechartRunnerCharts.FirstOrDefault(c => c.Name == current);
            OnPropertyChanged(nameof(SelectedStatechartRunnerChart));
            OnPropertyChanged(nameof(HasSelectedStatechartRunnerChart));
            StatechartRunnerStatus = $"Running '{current}'. Pick another chart to change it.";
        }
    }

    // Attach the picked chart to the selected node as a statechart_runner: compile it to IR as
    // authored (bindings resolve to the node names the chart names), then write the chart + chart_ir
    // config + events and save. One runner per node -- if the node already has one, its config is
    // updated in place rather than adding a second. The engine round-trips the config, so it
    // survives; the runner then runs on Play.
    private void AttachStatechartRunner()
    {
        if (!EnsureCanApply())
        {
            return;
        }

        if (_selectedRunnerChart is null)
        {
            StatechartRunnerStatus = "Pick a chart to run first.";
            return;
        }

        Chart chart;
        try
        {
            chart = StatechartJson.Load(File.ReadAllText(_selectedRunnerChart.Path));
        }
        catch (Exception e)
        {
            StatechartRunnerStatus = $"Couldn't read the chart: {e.Message}";
            return;
        }

        var chartIr = StatechartJson.Emit(chart, indented: false);

        // One runner per node: reuse the existing behavior (update its config), else add one. It is
        // NOT mirrored into the Behaviors list -- the card is its sole home.
        var runner = _inspectedSceneNode?.Behaviors.FirstOrDefault(b => b.Module == "statechart_runner");
        if (runner is null)
        {
            var response = _editorSession!.AddNodeBehavior(NodeId, "statechart_runner");
            if (!response.Ok)
            {
                LastEditError = response.Error;
                StatechartRunnerStatus = $"Couldn't attach: {response.Error}";
                return;
            }

            runner = new EngineSceneBehavior
            {
                Id = response.NodeId,
                Module = "statechart_runner",
                Label = _selectedRunnerChart.Name,
                Enabled = true,
                Events = new List<string>(StatechartRunnerAttachment.RunnerEvents),
            };
            _inspectedSceneNode?.Behaviors.Add(runner);
            NotifyComponentStateChanged();
        }

        _editorSession!.SetNodeBehaviorConfig(
            NodeId, runner.Id, StatechartRunnerAttachment.ConfigChart, "string", _selectedRunnerChart.Name);
        _editorSession.SetNodeBehaviorConfig(
            NodeId, runner.Id, StatechartRunnerAttachment.ConfigChartIr, "string", chartIr);
        _editorSession.SetNodeBehaviorEvents(
            NodeId, runner.Id, string.Join(Environment.NewLine, StatechartRunnerAttachment.RunnerEvents));
        _editorSession.SaveScene();

        // Mirror the config onto the local model so reselecting the node reflects the running chart.
        runner.Config.Clear();
        runner.Config.Add(new() { Name = StatechartRunnerAttachment.ConfigChart, Kind = "string", Value = _selectedRunnerChart.Name });
        runner.Config.Add(new() { Name = StatechartRunnerAttachment.ConfigChartIr, Kind = "string", Value = chartIr });

        HasAttachedStatechartRunner = true;
        LastEditError = string.Empty;
        StatechartRunnerStatus = $"Running '{_selectedRunnerChart.Name}'.";
    }

    // Remove the node's statechart_runner (the card's "Remove"); saves + hides the card.
    private void RemoveStatechartRunner()
    {
        if (!EnsureCanApply())
        {
            return;
        }

        var runner = _inspectedSceneNode?.Behaviors.FirstOrDefault(b => b.Module == "statechart_runner");
        if (runner is null)
        {
            HasStatechartRunnerSection = false;
            HasAttachedStatechartRunner = false;
            return;
        }

        var response = _editorSession!.RemoveNodeBehavior(NodeId, runner.Id);
        if (!response.Ok)
        {
            StatechartRunnerStatus = $"Couldn't remove: {response.Error}";
            return;
        }

        _inspectedSceneNode?.Behaviors.RemoveAll(b => b.Id == runner.Id);
        _editorSession.SaveScene();
        NotifyComponentStateChanged();

        HasAttachedStatechartRunner = false;
        HasStatechartRunnerSection = false;
        SelectedStatechartRunnerChart = null;
        StatechartRunnerStatus = string.Empty;
    }

    // ---- quantum_agent mind: point a node's quantum_agent at an authored mind graph ------------
    // Shown when the selected node has a quantum_agent behavior (the wave function). Picking a mind
    // compiles it to mind_ir + embeds it in the agent's config, which SUPERSEDES the scalar config.
    private Func<IReadOnlyList<MindFileInfo>>? _mindsProvider;
    private MindFileInfo? _selectedAgentMind;
    private string _quantumAgentMindStatus = string.Empty;
    private bool _hasQuantumAgentMindSection;

    public void SetMindsProvider(Func<IReadOnlyList<MindFileInfo>> provider) => _mindsProvider = provider;

    public bool HasQuantumAgentMindSection
    {
        get => _hasQuantumAgentMindSection;
        private set => SetProperty(ref _hasQuantumAgentMindSection, value);
    }

    public ObservableCollection<MindFileInfo> QuantumAgentMinds { get; } = [];

    public MindFileInfo? SelectedQuantumAgentMind
    {
        get => _selectedAgentMind;
        set
        {
            // Guard the transient null a ComboBox writes back while its ItemsSource
            // churns (RefreshQuantumAgentMindSection clears + repopulates QuantumAgentMinds
            // on every reselect / scenelet reopen). Without this, that writeback lands
            // AFTER the restore below set the field, silently resetting the picker to
            // "Pick a mind" even though the node's config names a mind -- the same
            // resets-to-none hazard the ref-agent mind picker guards against. Programmatic
            // clears assign the field directly (ResetQuantumAgentMindSelection).
            if (value is null || ReferenceEquals(value, _selectedAgentMind))
            {
                return;
            }
            _selectedAgentMind = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasSelectedQuantumAgentMind));
            // Picking a mind APPLIES it -- compile + embed + save, no separate button.
            // Every other inspector picker (render program, geometry, guard, host) applies
            // on pick; this one used to need a second "Attach mind" click, which read as
            // "I picked it, why didn't it save?". Programmatic restores assign the field
            // directly (Restore... / ResetQuantumAgentMindSelection) so they never re-apply.
            AttachMind();
        }
    }

    // Clear the picker selection past the setter's null guard (a real reset, not the
    // ComboBox's transient rebind null).
    private void ResetQuantumAgentMindSelection()
    {
        _selectedAgentMind = null;
        OnPropertyChanged(nameof(SelectedQuantumAgentMind));
        OnPropertyChanged(nameof(HasSelectedQuantumAgentMind));
    }

    public bool HasSelectedQuantumAgentMind => _selectedAgentMind is not null;

    public string QuantumAgentMindStatus
    {
        get => _quantumAgentMindStatus;
        private set
        {
            if (SetProperty(ref _quantumAgentMindStatus, value))
            {
                OnPropertyChanged(nameof(HasQuantumAgentMindStatus));
            }
        }
    }

    public bool HasQuantumAgentMindStatus => !string.IsNullOrEmpty(_quantumAgentMindStatus);

    public IRelayCommand AttachMindCommand { get; }

    public IRelayCommand DetachMindCommand { get; }

    private EngineSceneBehavior? InspectedQuantumAgent() =>
        _inspectedSceneNode?.Behaviors.FirstOrDefault(b => b.Module == QuantumAgentMindAttachment.Module);

    private void RefreshQuantumAgentMindSection()
    {
        ResetQuantumAgentMindSelection();
        QuantumAgentMindStatus = string.Empty;
        QuantumAgentMinds.Clear();
        if (HasSceneNodeSelection && _mindsProvider is not null)
        {
            foreach (var mind in _mindsProvider())
            {
                QuantumAgentMinds.Add(mind);
            }
        }

        var agent = InspectedQuantumAgent();
        HasQuantumAgentMindSection = agent is not null;

        var current = agent?.Config.FirstOrDefault(c => c.Name == QuantumAgentMindAttachment.ConfigMind)?.Value;
        var hasIr = !string.IsNullOrEmpty(
            agent?.Config.FirstOrDefault(c => c.Name == QuantumAgentMindAttachment.ConfigMindIr)?.Value);
        if (!string.IsNullOrEmpty(current))
        {
            _selectedAgentMind = QuantumAgentMinds.FirstOrDefault(m => m.Name == current);
            OnPropertyChanged(nameof(SelectedQuantumAgentMind));
            OnPropertyChanged(nameof(HasSelectedQuantumAgentMind));
            QuantumAgentMindStatus = $"Using mind '{current}'. Pick another to change it.";
        }
        else
        {
            QuantumAgentMindStatus = hasIr ? "A mind is attached. Pick one to change it." : string.Empty;
        }
    }

    // Attach the picked mind: compile it to mind_ir + write it (with the source name) onto the
    // node's quantum_agent, then save. The engine round-trips the config; the mind supersedes the
    // agent's scalar config on Play.
    private void AttachMind()
    {
        if (!EnsureCanApply())
        {
            return;
        }

        if (_selectedAgentMind is null)
        {
            QuantumAgentMindStatus = "Pick a mind first.";
            return;
        }

        var agent = InspectedQuantumAgent();
        if (agent is null)
        {
            QuantumAgentMindStatus = "This node has no quantum_agent to attach a mind to.";
            return;
        }

        string mindIr;
        try
        {
            mindIr = MindJson.Emit(MindJson.Load(File.ReadAllText(_selectedAgentMind.Path)), indented: false);
        }
        catch (Exception e)
        {
            QuantumAgentMindStatus = $"Couldn't compile the mind: {e.Message}";
            return;
        }

        _editorSession!.SetNodeBehaviorConfig(
            NodeId, agent.Id, QuantumAgentMindAttachment.ConfigMind, "string", _selectedAgentMind.Name);
        _editorSession.SetNodeBehaviorConfig(
            NodeId, agent.Id, QuantumAgentMindAttachment.ConfigMindIr, "string", mindIr);
        _editorSession.SaveScene();

        SetLocalBehaviorConfig(agent, QuantumAgentMindAttachment.ConfigMind, _selectedAgentMind.Name);
        SetLocalBehaviorConfig(agent, QuantumAgentMindAttachment.ConfigMindIr, mindIr);

        LastEditError = string.Empty;
        QuantumAgentMindStatus = $"Using mind '{_selectedAgentMind.Name}'.";
    }

    // Detach: clear the mind_ir (empty reads as absent), so the quantum_agent falls back to its
    // scalar config. Leaves the quantum_agent behavior itself in place.
    private void DetachMind()
    {
        if (!EnsureCanApply())
        {
            return;
        }

        var agent = InspectedQuantumAgent();
        if (agent is null)
        {
            return;
        }

        _editorSession!.SetNodeBehaviorConfig(
            NodeId, agent.Id, QuantumAgentMindAttachment.ConfigMindIr, "string", string.Empty);
        _editorSession.SetNodeBehaviorConfig(
            NodeId, agent.Id, QuantumAgentMindAttachment.ConfigMind, "string", string.Empty);
        _editorSession.SaveScene();

        SetLocalBehaviorConfig(agent, QuantumAgentMindAttachment.ConfigMindIr, string.Empty);
        SetLocalBehaviorConfig(agent, QuantumAgentMindAttachment.ConfigMind, string.Empty);

        ResetQuantumAgentMindSelection();
        QuantumAgentMindStatus = "Detached — the quantum_agent uses its scalar config.";
    }

    // The config value is init-only, so replace the row (remove + re-add) rather than mutate it.
    private static void SetLocalBehaviorConfig(EngineSceneBehavior behavior, string name, string value)
    {
        behavior.Config.RemoveAll(c => c.Name == name);
        behavior.Config.Add(new() { Name = name, Kind = "string", Value = value });
    }

    public void Inspect(AssetGraphNodeCardViewModel? node)
    {
        Components.Clear();
        Behaviors.Clear();
        AvailableBehaviorModules.Clear();
        AssetGraphInputPorts.Clear();
        AssetGraphOutputPorts.Clear();
        AssetGraphDiagnostics.Clear();
        AssetGraphParams.Clear();
        ClearGlbNodePicker();
        LastEditError = string.Empty;
        // Re-evaluate whether editing is available for the freshly-selected node
        // (the viewport may have started/stopped since the last selection).
        RefreshEditAvailability();

        if (node is null)
        {
            Header = string.Empty;
            EmptyState = "No scene or asset graph node selected.";
            SetSelectionKind(InspectorSelectionKind.None);
            ClearNodeFields();
            ClearAssetGraphFields();
            NotifyComponentStateChanged();
            NotifyAssetGraphPortStateChanged();
            return;
        }

        Header = node.DisplayName;
        EmptyState = string.Empty;
        SetSelectionKind(InspectorSelectionKind.AssetGraphNode);
        ClearNodeFields();

        _assetGraphNodeIdValue = node.Id;
        AssetGraphNodeId = node.Id.ToString(CultureInfo.InvariantCulture);
        AssetGraphNodeName = node.DisplayName;
        AssetGraphNodeType = node.TypeName;
        AssetGraphNodeTypeId = node.Type.ToString(CultureInfo.InvariantCulture);
        AssetGraphNodeSchema = node.SchemaDisplay;
        AssetGraphNodeCompileStatus = node.CompileStatus;
        AssetGraphNodePosition =
            $"{FormatDouble(node.GraphX)}, {FormatDouble(node.GraphY)}";

        _suppressLiveEdits = true;
        try
        {
            NodeRerouteName = _reroutes?.NameOf(node.Id) ?? string.Empty;
        }
        finally
        {
            _suppressLiveEdits = false;
        }

        foreach (var port in node.InputPorts)
        {
            AssetGraphInputPorts.Add(new InspectorAssetGraphPortViewModel(port));
        }

        foreach (var port in node.OutputPorts)
        {
            AssetGraphOutputPorts.Add(new InspectorAssetGraphPortViewModel(port));
        }

        foreach (var diagnostic in node.Diagnostics)
        {
            AssetGraphDiagnostics.Add(
                new InspectorAssetGraphDiagnosticViewModel(diagnostic));
        }

        // For the "Mesh from GLB scene" extractor (issue #213), the `node_id` param
        // is set EXCLUSIVELY through the "GLB node" tree picker below, so it is hidden
        // from the generic param editor (a free-typed id that doesn't match a GLB node
        // just fails the compile). Every other param, and every other node, is listed
        // unchanged.
        var hideNodeIdParam = IsMeshFromGlbSceneNode(node);
        foreach (var param in node.Params)
        {
            if (hideNodeIdParam
                && string.Equals(param.Name, "node_id", StringComparison.Ordinal))
            {
                continue;
            }

            AssetGraphParams.Add(new InspectorAssetGraphParamViewModel(
                param,
                ApplyAssetGraphNodeParam));
        }

        // "Mesh from GLB scene" nodes get the "GLB node" tree picker (issue #213):
        // resolve the connected GLB and show its hierarchy as a pickable tree.
        if (hideNodeIdParam)
        {
            PopulateGlbNodePicker(node);
        }

        NotifyComponentStateChanged();
        NotifyAssetGraphPortStateChanged();
    }

    // Inspect a sub-graph proxy (issue woguls/wozzits-editor#1): show its editable name so
    // authors can label groups. Rename is pure editor view-state — no engine/session call.
    public void Inspect(AssetGraphSubGraph? subGraph)
    {
        Components.Clear();
        Behaviors.Clear();
        AvailableBehaviorModules.Clear();
        AssetGraphInputPorts.Clear();
        AssetGraphOutputPorts.Clear();
        AssetGraphDiagnostics.Clear();
        AssetGraphParams.Clear();
        ClearGlbNodePicker();
        LastEditError = string.Empty;

        if (subGraph is null)
        {
            Header = string.Empty;
            EmptyState = "No scene or asset graph node selected.";
            SetSelectionKind(InspectorSelectionKind.None);
            ClearNodeFields();
            ClearAssetGraphFields();
            ClearSubGraphFields();
            NotifyComponentStateChanged();
            NotifyAssetGraphPortStateChanged();
            return;
        }

        Header = subGraph.Name;
        EmptyState = string.Empty;
        SetSelectionKind(InspectorSelectionKind.SubGraph);
        ClearNodeFields();
        ClearAssetGraphFields();

        _inspectedSubGraph = subGraph;
        _suppressLiveEdits = true;
        try
        {
            SubGraphName = subGraph.Name;
            SubGraphMemberCount =
                subGraph.MemberCount.ToString(CultureInfo.InvariantCulture);
        }
        finally
        {
            _suppressLiveEdits = false;
        }

        NotifyComponentStateChanged();
        NotifyAssetGraphPortStateChanged();
    }

    private void OnSubGraphNameEdited()
    {
        if (_inspectedSubGraph is null
            || !HasSubGraphSelection
            || string.IsNullOrWhiteSpace(SubGraphName))
        {
            return;
        }

        _inspectedSubGraph.Name = SubGraphName;
        Header = SubGraphName;
    }

    private void ClearSubGraphFields()
    {
        _inspectedSubGraph = null;
        _suppressLiveEdits = true;
        try
        {
            SubGraphName = string.Empty;
            SubGraphMemberCount = string.Empty;
        }
        finally
        {
            _suppressLiveEdits = false;
        }
    }

    private void ApplyAssetGraphNodeParam(string name, string value)
    {
        if (!HasAssetGraphNodeSelection)
        {
            LastEditError = "No asset graph node is selected.";
            return;
        }
        if (_editorSession is null)
        {
            LastEditError = "Engine editor session is not available.";
            return;
        }

        var response = _editorSession.SetAssetGraphNodeParamString(
            _assetGraphNodeIdValue,
            name,
            value);
        SetEditResponse(response);

        // The engine draft now has the new value, but the graph pane's node card
        // does not. Ask the host to re-pull the live graph so re-selecting the
        // node shows the applied value instead of the stale default (#218 Phase 3).
        if (response.Ok)
        {
            AssetGraphNodeParamApplied?.Invoke();
        }
    }

    // Live properties push: as the name/visibility fields change, mirror them
    // into the running engine and reflect the label in the tree. Suppressed
    // while a node's values are being loaded into the fields.
    private void OnNodePropertiesEdited()
    {
        if (_suppressLiveEdits
            || !HasSceneNodeSelection
            || string.IsNullOrWhiteSpace(NodeId)
            || _editorSession is null)
        {
            return;
        }

        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.DisplayName = NodeName;
            // Mirror visibility back onto the tree node too, so re-selecting the
            // node reflects the edit instead of reverting to the snapshot value
            // (issue #213): the checkbox was reading the stale node.Visible.
            _inspectedSceneNode.Visible = NodeVisible;
        }

        _editorSession.SetSceneNodePropertiesLive(NodeId, NodeName, NodeVisible);
    }

    // Live render-layer push: as the dropdown changes, set the node's render_order
    // in the running engine (which re-bakes draw order) and mirror it onto the
    // tree node so a re-select shows the new layer. Suppressed while populating,
    // and no-op'd (with a log) when the viewport is down, via EnsureCanApply.
    private void OnRenderLayerEdited()
    {
        if (_suppressLiveEdits || _selectedRenderLayer is null)
        {
            return;
        }
        if (!EnsureCanApply())
        {
            return;
        }
        SetEditResponse(
            _editorSession!.SetNodeRenderOrder(NodeId, _selectedRenderLayer.Value));
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.RenderOrder = _selectedRenderLayer.Value;
        }
    }

    // Live preview push: as the transform fields change, mirror them into the
    // running viewport engine. Suppressed while a node's values are being
    // loaded into the fields so selecting a node doesn't echo back.
    private void PushLiveTransform()
    {
        if (_suppressLiveEdits
            || !HasSceneNodeSelection
            || string.IsNullOrWhiteSpace(NodeId)
            || _editorSession is null)
        {
            return;
        }

        _editorSession.SetSceneNodeTransformLive(NodeId, CurrentTransformEdit());

        // Keep the editor's cached scene-node model in step with the live edit so
        // re-selecting this node repopulates the inspector from the edited values
        // instead of snapping back to the startup snapshot. Only Display is read
        // post-snapshot (SetTransformFields); the raw numeric lists are not, and
        // the runtime stays authoritative for the actual transform and for
        // save_scene, so refreshing Display is sufficient.
        if (_inspectedSceneNode?.Transform is { } current)
        {
            _inspectedSceneNode.Transform = current with
            {
                Display = new EngineSceneTransformDisplay
                {
                    TranslationX = TranslationX,
                    TranslationY = TranslationY,
                    TranslationZ = TranslationZ,
                    RotationX = RotationX,
                    RotationY = RotationY,
                    RotationZ = RotationZ,
                    ScaleX = ScaleX,
                    ScaleY = ScaleY,
                    ScaleZ = ScaleZ,
                },
            };
        }
    }

    // Persist the live scene edits to disk. Wired to inspector-field blur (not
    // per keystroke / not mid-drag), so transform / name / visibility edits
    // survive a relaunch. save_scene is dirty-gated and serializes the live
    // runtime scene, so this is a cheap no-op when nothing scene-side changed.
    public void PersistSceneEditsOnBlur()
    {
        _editorSession?.SaveScene();
    }

    private EngineSceneTransformEdit CurrentTransformEdit()
    {
        return new EngineSceneTransformEdit
        {
            TranslationX = TranslationX,
            TranslationY = TranslationY,
            TranslationZ = TranslationZ,
            RotationX = RotationX,
            RotationY = RotationY,
            RotationZ = RotationZ,
            ScaleX = ScaleX,
            ScaleY = ScaleY,
            ScaleZ = ScaleZ,
        };
    }

    private void ApplyCamera()
    {
        if (!EnsureCanApply())
        {
            return;
        }

        SetEditResponse(_editorSession!.SetSceneNodeCamera(
            NodeId,
            new EngineSceneCameraEdit
            {
                FieldOfViewY = CameraFovY,
                NearPlane = CameraNear,
                FarPlane = CameraFar,
                Aspect = CameraAspect,
            }));
    }

    private bool EnsureCanApply()
    {
        if (!HasSceneNodeSelection || string.IsNullOrWhiteSpace(NodeId))
        {
            LastEditError = "No scene node is selected.";
            return false;
        }
        if (_editorSession is null)
        {
            LastEditError = "Engine editor session is not available.";
            return false;
        }
        // Scene-node edits run against the live viewport runtime; with it down
        // they would silently no-op. Refuse and log instead so the user knows
        // why, mirroring the scene tree's RequireRuntime. (The edit surface is
        // also disabled via CanEditNode; this is the always-fresh safety net for
        // the case where the viewport stopped after the node was selected.)
        if (!_editorSession.IsRuntimeRunning)
        {
            LastEditError =
                "The viewport is not running; scene-node edits are unavailable.";
            _log?.Invoke(
                "[editor] Scene-node edit requires the running viewport, which "
                + "is not running; reopen it (Restart Viewport) and try again.");
            return false;
        }
        return true;
    }

    private void SetEditResponse(EngineMutationResponse response)
    {
        LastEditError = response.Ok ? string.Empty : response.Error;
    }

    private void ClearNodeFields()
    {
        _inspectedSceneNode = null;
        _suppressLiveEdits = true;
        try
        {
            NodeId = string.Empty;
            NodeName = string.Empty;
            ParentId = string.Empty;
            NodeVisible = false;
            SelectedRenderLayer = null;
            HasRenderableReference = false;
            RenderableSource = string.Empty;
            RenderableSourceKind = string.Empty;
            RenderableAssetGraphNodeId = string.Empty;
            SelectedSceneSourceOption = null;
            SubtreeReferenceLabel = string.Empty;
            HasSubtreeSection = false;
            ResetRenderProgramState();
            ResetGeometryState();
            ResetRenderableIngredientsState();
            ResetCollisionState();
            ResetMotionState();
            ResetMotionFilterState();
            ResetAtmosphereState();
            ResetEnvironmentState();
            ComponentsHeader = "Components";
            SetTransformFields(null);
            HasCameraComponent = false;
            CameraFovY = string.Empty;
            CameraNear = string.Empty;
            CameraFar = string.Empty;
            CameraAspect = string.Empty;
        }
        finally
        {
            _suppressLiveEdits = false;
        }
    }

    private void ClearAssetGraphFields()
    {
        _assetGraphNodeIdValue = 0;
        AssetGraphNodeId = string.Empty;
        AssetGraphNodeName = string.Empty;
        AssetGraphNodeType = string.Empty;
        AssetGraphNodeTypeId = string.Empty;
        AssetGraphNodeSchema = string.Empty;
        AssetGraphNodeCompileStatus = string.Empty;
        AssetGraphNodePosition = string.Empty;
        NodeRerouteName = string.Empty;
    }

    // Statechart selection (E3b) routed from the open chart document. The selected view-model
    // carries the read-only detail (phase-2 PropertyRows / effects); editing is 3b-ii.
    public void Inspect(DataflowNodeViewModel? node)
    {
        SelectedStatechartState = null;
        SelectedStatechartNode = node;
        if (node is null)
        {
            Header = string.Empty;
            EmptyState = "No node selected.";
            SetSelectionKind(InspectorSelectionKind.None);
            return;
        }

        Header = node.Title;
        EmptyState = string.Empty;
        SetSelectionKind(InspectorSelectionKind.StatechartDataflowNode);
    }

    public void Inspect(StateNodeViewModel? state)
    {
        SelectedStatechartNode = null;
        SelectedStatechartState = state;
        if (state is null)
        {
            Header = string.Empty;
            EmptyState = "No state selected.";
            SetSelectionKind(InspectorSelectionKind.None);
            return;
        }

        Header = state.Title;
        EmptyState = string.Empty;
        SetSelectionKind(InspectorSelectionKind.StatechartState);
    }

    private void SetSelectionKind(InspectorSelectionKind kind)
    {
        if (!SetProperty(ref _selectionKind, kind, nameof(HasSelection)))
        {
            return;
        }

        OnPropertyChanged(nameof(HasNoSelection));
        OnPropertyChanged(nameof(HasSceneNodeSelection));
        OnPropertyChanged(nameof(HasAssetGraphNodeSelection));
        OnPropertyChanged(nameof(HasSubGraphSelection));
        OnPropertyChanged(nameof(HasStatechartNodeSelection));
        OnPropertyChanged(nameof(HasStatechartStateSelection));
        ApplyCameraCommand.NotifyCanExecuteChanged();
    }

    private void SetTransformFields(EngineSceneTransform? transform)
    {
        // Populating fires the setters; callers (Inspect / ClearNodeFields) hold
        // _suppressLiveEdits so this doesn't echo back to the engine.
        HasTransform = transform is not null;
        TranslationX = transform?.Display.TranslationX ?? string.Empty;
        TranslationY = transform?.Display.TranslationY ?? string.Empty;
        TranslationZ = transform?.Display.TranslationZ ?? string.Empty;

        RotationX = transform?.Display.RotationX ?? string.Empty;
        RotationY = transform?.Display.RotationY ?? string.Empty;
        RotationZ = transform?.Display.RotationZ ?? string.Empty;

        ScaleX = transform?.Display.ScaleX ?? string.Empty;
        ScaleY = transform?.Display.ScaleY ?? string.Empty;
        ScaleZ = transform?.Display.ScaleZ ?? string.Empty;
    }

    private void SetComponentFields(SceneTreeNodeViewModel node)
    {
        foreach (var component in node.Components)
        {
            // Camera, Collision, Motion, Audio Source, and Atmosphere are shown +
            // removed via their own parameter sections below, not as generic rows.
            if (string.Equals(component.Kind, "camera", StringComparison.Ordinal)
                || string.Equals(component.Kind, "collision", StringComparison.Ordinal)
                || string.Equals(component.Kind, "motion", StringComparison.Ordinal)
                || string.Equals(component.Kind, "motion_filter", StringComparison.Ordinal)
                || string.Equals(component.Kind, "audio_source", StringComparison.Ordinal)
                || string.Equals(component.Kind, "atmosphere", StringComparison.Ordinal))
            {
                continue;
            }
            Components.Add(new InspectorComponentViewModel(
                component.DisplayName,
                component.Kind,
                RemoveComponent));
        }

        // Collision / Motion field sections (terrain-stick track): revealed when the
        // node carries the component, with the persisted field values restored from
        // the snapshot (read-back gap fix) so a fresh select / reload shows the saved
        // values instead of resetting to defaults. Runs under _suppressLiveEdits
        // (held by Inspect) so populating the fields doesn't echo a live edit.
        RestoreCollisionState(node);
        RestoreMotionState(node);
        RestoreMotionFilterState(node);
        RestoreAudioSourceState(node);
        RestoreAtmosphereState(node);
        RestoreEnvironmentState(node);

        if (node.Camera is not null)
        {
            HasCameraComponent = true;
            CameraFovY = FormatNullable(node.Camera.FieldOfViewY);
            CameraNear = FormatNullable(node.Camera.NearPlane);
            CameraFar = FormatNullable(node.Camera.FarPlane);
            CameraAspect = FormatNullable(node.Camera.Aspect);
        }
        else
        {
            HasCameraComponent = false;
            CameraFovY = string.Empty;
            CameraNear = string.Empty;
            CameraFar = string.Empty;
            CameraAspect = string.Empty;
        }

        foreach (var behavior in node.Behaviors)
        {
            if (string.Equals(behavior.Module, "statechart_runner", StringComparison.Ordinal))
            {
                continue;   // shown + managed by the Statechart runner card, not as a raw behavior row
            }

            Behaviors.Add(CreateBehaviorViewModel(behavior));
        }
    }

    private InspectorBehaviorViewModel CreateBehaviorViewModel(
        EngineSceneBehavior behavior)
    {
        return new InspectorBehaviorViewModel(
            behavior,
            DeclaredParamsFor(behavior.Module),
            SetBehaviorEnabled,
            ApplyBehaviorFields,
            ApplyBehaviorEvents,
            RemoveBehavior,
            WriteBehaviorConfig)
        {
            // Carry over a failure from the last rebuild, so selecting the node again
            // still shows why this module's card looks stale.
            BuildErrors = JoinErrorsFor(behavior.Module),
        };
    }

    // The config params a behavior MODULE declares (key/type/default), so the inspector
    // Diagnostics from the LAST behavior rebuild; empty when it built clean. A failed
    // rebuild used to be one line in a console full of live behavior output, so the
    // stale DLL that resulted looked like a broken editor. These surface on the
    // Behaviors section and on the card whose module the compiler named.
    private IReadOnlyList<string> _buildErrors = [];

    public void SetBuildErrors(IReadOnlyList<string>? errors)
    {
        _buildErrors = errors ?? [];
        OnPropertyChanged(nameof(HasBuildFailure));
        OnPropertyChanged(nameof(BuildFailureSummary));
        OnPropertyChanged(nameof(BuildErrorText));
        // Update the cards already on screen -- a rebuild usually happens while the node
        // you are working on is still selected, so waiting for a reselect is too late.
        foreach (var behavior in Behaviors)
        {
            behavior.BuildErrors = JoinErrorsFor(behavior.Module);
        }
    }

    public bool HasBuildFailure => _buildErrors.Count > 0;

    public string BuildFailureSummary =>
        $"Last rebuild FAILED ({_buildErrors.Count} error"
        + (_buildErrors.Count == 1 ? "" : "s")
        + "). The modules were NOT reloaded — the engine is still running the previous build.";

    public string BuildErrorText => string.Join("\n", _buildErrors);

    // Attribute a diagnostic to a module by name. The project convention is that a
    // module's CMake target, folder and source file all carry the module name
    // (add_terrain_collision_behavior_module(enemy_tank_v1 enemy_tank_v1/enemy_tank_v1.cpp)),
    // so the compiler's file path names it. It is a heuristic -- which is why the whole
    // list ALSO shows on the section banner, so nothing is ever hidden by a miss.
    private string JoinErrorsFor(string module) =>
        string.Join(
            "\n",
            string.IsNullOrEmpty(module)
                ? []
                : _buildErrors.Where(
                    e => e.Contains(module, StringComparison.OrdinalIgnoreCase)));

    // can render typed fields. Pulled lazily from the device-free, project-aware module
    // catalog (built-ins + this project's own behavior DLLs) and cached -- rebuilding it
    // reloads every project DLL, so it is not something to do per selection.
    private EngineBehaviorModuleCatalogResponse? _moduleParamCatalog;

    // Drop the cached schema and repopulate the open node. A rebuild reloads the DLLs into
    // the engine, but the params a module DECLARES can change with them -- and nothing used
    // to tell the inspector its cached schema was stale. The card then kept rendering the
    // OLD params (i.e. none, for a module that just grew some) until the editor restarted,
    // which reads as "the editor ignored my change".
    public void RefreshDeclaredParams()
    {
        _moduleParamCatalog = null;
        if (_inspectedSceneNode is not null)
        {
            Inspect(_inspectedSceneNode);   // re-pulls the catalog while rebuilding the cards
        }
    }

    private IReadOnlyList<EngineBehaviorModuleParam> DeclaredParamsFor(string module)
    {
        _moduleParamCatalog ??= _editorSession?.LoadBehaviorModuleCatalog();
        var match = _moduleParamCatalog?.Modules
            .FirstOrDefault(m => m.Module == module);
        return match?.Params ?? [];
    }

    // Persist one behavior-config value live: SetNodeBehaviorConfig on the running
    // engine, save, and mirror onto the local model so reselecting the node reflects it.
    private void WriteBehaviorConfig(
        string behaviorId, string key, string kind, string value)
    {
        if (_editorSession is null || !EnsureCanApply())
        {
            return;
        }

        var response = _editorSession.SetNodeBehaviorConfig(
            NodeId, behaviorId, key, kind, value);
        if (!response.Ok)
        {
            LastEditError = response.Error;
            return;
        }

        LastEditError = string.Empty;
        _editorSession.SaveScene();

        var behavior = _inspectedSceneNode?.Behaviors
            .FirstOrDefault(b => b.Id == behaviorId);
        if (behavior is not null)
        {
            behavior.Config.RemoveAll(c => c.Name == key);
            behavior.Config.Add(new EngineSceneBehaviorConfig
            {
                Name = key,
                Kind = kind,
                Value = value,
            });
        }
    }

    // Add a behavior binding for the typed module to the selected node (the
    // free-text fallback for a module not yet imported).
    private void AddBehavior()
    {
        AddBehaviorModule(NewBehaviorModule);
    }

    // Add a behavior binding for `moduleName` to the selected node, live on the
    // running engine; on success mint a local row so reselection is stable. The
    // shared core behind both the free-text Add and the "+"-from-modules menu.
    private void AddBehaviorModule(string? moduleName)
    {
        if (!EnsureCanApply())
        {
            return;
        }

        var module = (moduleName ?? string.Empty).Trim();
        if (string.IsNullOrEmpty(module))
        {
            LastEditError = "Enter a behavior module name to add.";
            return;
        }

        var response = _editorSession!.AddNodeBehavior(NodeId, module);
        if (!response.Ok)
        {
            LastEditError = response.Error;
            return;
        }

        LastEditError = string.Empty;
        var added = new EngineSceneBehavior
        {
            Id = response.NodeId,
            Module = module,
            Enabled = true,
        };
        _inspectedSceneNode?.Behaviors.Add(added);
        Behaviors.Add(CreateBehaviorViewModel(added));
        NewBehaviorModule = string.Empty;
        // A quantum_agent reveals the Mind picker; a statechart_runner its chart picker.
        // Recompute both from the now-updated behavior list, so an ADDED behavior surfaces
        // its section immediately instead of only on the next reselect (mirrors
        // RemoveBehavior, which already refreshes these).
        RefreshQuantumAgentMindSection();
        RefreshStatechartRunnerSection();
        NotifyComponentStateChanged();
    }

    // Re-query the engine's registered modules so the "+" add menu reflects the
    // current set whenever it is opened. The engine loads modules asynchronously
    // (the UI is live before load_scene finishes) and a behavior rebuild changes
    // them, so the list captured at node-selection time can be stale or empty.
    // The view calls this as the flyout opens.
    public void RefreshBehaviorModuleCatalog()
    {
        if (HasSceneNodeSelection)
        {
            RefreshAvailableBehaviorModules();
            _log?.Invoke(
                $"[editor] behavior module picker: {AvailableBehaviorModules.Count} "
                + "imported module(s)");
        }
    }

    // Rebuild the "+"-menu options from the engine's registered modules. Each
    // option closes over AddBehaviorModule so the MenuFlyout item binds to its
    // own command (a flyout popup can't reach the inspector's DataContext).
    private void RefreshAvailableBehaviorModules()
    {
        AvailableBehaviorModules.Clear();
        if (_editorSession is not null)
        {
            foreach (var module in _editorSession.GetBehaviorModuleCatalog())
            {
                if (string.Equals(module, "statechart_runner", StringComparison.Ordinal))
                {
                    continue;   // added via the Components "+" -> Statechart Runner card, not here
                }

                AvailableBehaviorModules.Add(
                    new InspectorBehaviorModuleOptionViewModel(
                        module,
                        AddBehaviorModule));
            }
        }
        OnPropertyChanged(nameof(HasAvailableBehaviorModules));
    }

    private void SetBehaviorEnabled(InspectorBehaviorViewModel behavior)
    {
        if (!EnsureCanApply())
        {
            return;
        }
        SetEditResponse(_editorSession!.SetNodeBehaviorEnabled(
            NodeId, behavior.Id, behavior.Enabled));
    }

    private void ApplyBehaviorFields(InspectorBehaviorViewModel behavior)
    {
        if (!EnsureCanApply())
        {
            return;
        }
        SetEditResponse(_editorSession!.SetNodeBehaviorFields(
            NodeId, behavior.Id, behavior.Label, behavior.Module));
    }

    private void ApplyBehaviorEvents(InspectorBehaviorViewModel behavior)
    {
        if (!EnsureCanApply())
        {
            return;
        }
        SetEditResponse(_editorSession!.SetNodeBehaviorEvents(
            NodeId, behavior.Id, behavior.Events));
    }

    private void RemoveBehavior(InspectorBehaviorViewModel behavior)
    {
        if (!EnsureCanApply())
        {
            return;
        }
        var response = _editorSession!.RemoveNodeBehavior(NodeId, behavior.Id);
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }
        Behaviors.Remove(behavior);
        _inspectedSceneNode?.Behaviors.RemoveAll(b => b.Id == behavior.Id);
        // A removed behavior can strand a sub-card that backs onto it: the Mind card needs
        // a quantum_agent, the Statechart Runner card needs a statechart_runner. These are
        // recomputed on node selection, not on removal, so re-evaluate them here -- otherwise
        // the Mind "Detach" card lingers after its quantum_agent is gone.
        RefreshQuantumAgentMindSection();
        RefreshStatechartRunnerSection();
        NotifyComponentStateChanged();
    }

    private void AddComponent(string? kind)
    {
        if (!EnsureCanApply() || string.IsNullOrWhiteSpace(kind))
        {
            return;
        }

        // Renderable is the asset-graph reference, not a default-toggle
        // component: reveal its editor so the user sets the asset-graph node id
        // (applied via the Renderable Asset section), not the generic verb (the
        // engine rejects "renderable" there). The legacy slot is never touched.
        if (string.Equals(kind, "renderable", StringComparison.Ordinal))
        {
            HasRenderableReference = true;
            LastEditError = string.Empty;
            return;
        }

        // "Subtree from asset" is the same shape as renderable: an asset-graph
        // reference, not a default-toggle component. Reveal its picker section so the
        // user references a "Scene from GLB" node (applied via SetNodeSceneSource);
        // do NOT call the generic verb (the engine rejects "subtree_from_asset").
        if (string.Equals(kind, "subtree_from_asset", StringComparison.Ordinal))
        {
            HasSubtreeSection = true;
            LastEditError = string.Empty;
            return;
        }

        // "Render program" is likewise an asset-graph reference, not a default-toggle
        // component. Reveal its picker section; the dedicated program verb applies on
        // pick, so do NOT call the generic verb (the engine rejects "render_program").
        if (string.Equals(kind, "render_program", StringComparison.Ordinal))
        {
            HasRenderProgramSection = true;
            LastEditError = string.Empty;
            return;
        }

        // "Geometry" is the MESH half of the same renderable binding (issue #213
        // increment 2): reveal its picker; the dedicated geometry verb applies on
        // pick, so do NOT call the generic verb (the engine rejects "geometry").
        if (string.Equals(kind, "geometry", StringComparison.Ordinal))
        {
            HasGeometrySection = true;
            LastEditError = string.Empty;
            return;
        }

        // "Statechart runner" is an authored-chart behavior, not a default-toggle component:
        // reveal its card (a chart picker); the runner is added + configured on Attach.
        if (string.Equals(kind, "statechart_runner", StringComparison.Ordinal))
        {
            HasStatechartRunnerSection = true;
            LastEditError = string.Empty;
            return;
        }

        var response = _editorSession!.AddNodeComponent(NodeId, kind);
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }

        // Reflect immediately (the snapshot reconciles on its next refresh).
        // Camera / Collision / Motion have their own parameter sections; the rest
        // list as removable rows. In every case mirror the add onto the cached
        // tree-node VM, or re-selecting the node re-derives the components from the
        // stale snapshot and the add reverts (the renderable-revert bug, generalized
        // — mirrors Behaviors).
        if (string.Equals(kind, "camera", StringComparison.Ordinal))
        {
            HasCameraComponent = true;
            // Seed the optimistic component with the engine's canonical camera
            // defaults (which AddNodeComponent already applied engine-side), so
            // the inspector shows real values instead of blanks that would apply
            // back as a zero-FOV camera (#220).
            var camera = EngineSceneCamera.CreateEngineDefaults();
            if (_inspectedSceneNode is { Camera: null } cameraNode)
            {
                cameraNode.Camera = camera;
            }
            CameraFovY = FormatNullable(camera.FieldOfViewY);
            CameraNear = FormatNullable(camera.NearPlane);
            CameraFar = FormatNullable(camera.FarPlane);
            CameraAspect = FormatNullable(camera.Aspect);
        }
        else if (string.Equals(kind, "collision", StringComparison.Ordinal))
        {
            HasCollisionComponent = true;
            MirrorComponentAdded(kind);
        }
        else if (string.Equals(kind, "motion", StringComparison.Ordinal))
        {
            HasMotionComponent = true;
            MirrorComponentAdded(kind);
        }
        else if (string.Equals(kind, "motion_filter", StringComparison.Ordinal))
        {
            HasMotionFilterComponent = true;
            MirrorComponentAdded(kind);
        }
        else if (string.Equals(kind, "audio_source", StringComparison.Ordinal))
        {
            HasAudioSourceComponent = true;
            MirrorComponentAdded(kind);
        }
        else if (string.Equals(kind, "atmosphere", StringComparison.Ordinal))
        {
            HasAtmosphereComponent = true;
            MirrorComponentAdded(kind);
        }
        else if (string.Equals(kind, "environment", StringComparison.Ordinal))
        {
            HasEnvironmentComponent = true;
            MirrorComponentAdded(kind);
        }
        else if (!Components.Any(
            c => string.Equals(c.Kind, kind, StringComparison.Ordinal)))
        {
            Components.Add(new InspectorComponentViewModel(
                ComponentDisplayName(kind), kind, RemoveComponent));
            MirrorComponentAdded(kind);
            NotifyComponentStateChanged();
        }
    }

    private void RemoveCameraComponent()
    {
        if (!EnsureCanApply())
        {
            return;
        }
        var response = _editorSession!.RemoveNodeComponent(NodeId, "camera");
        SetEditResponse(response);
        if (response.Ok)
        {
            HasCameraComponent = false;
            // Clear the cached camera too, or reselect would read the stale snapshot
            // camera and bring the section back.
            if (_inspectedSceneNode is not null)
            {
                _inspectedSceneNode.Camera = null;
            }
        }
    }

    private void RemoveComponent(InspectorComponentViewModel component)
    {
        if (!EnsureCanApply())
        {
            return;
        }

        var response = _editorSession!.RemoveNodeComponent(NodeId, component.Kind);
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }

        Components.Remove(component);
        MirrorComponentRemoved(component.Kind);
        // Removing the camera also hides its parameter section.
        if (string.Equals(component.Kind, "camera", StringComparison.Ordinal))
        {
            HasCameraComponent = false;
        }
        NotifyComponentStateChanged();
    }

    // Keep the cached tree-node VM's component list in step with a live add/remove,
    // so re-selecting the node (which rebuilds the inspector rows from that list)
    // reflects the change instead of reverting to the startup snapshot.
    private void MirrorComponentAdded(string kind)
    {
        if (_inspectedSceneNode is { } node
            && !node.Components.Any(
                c => string.Equals(c.Kind, kind, StringComparison.Ordinal)))
        {
            node.Components.Add(new EngineSceneComponent
            {
                Kind = kind,
                DisplayName = ComponentDisplayName(kind),
            });
        }
    }

    private void MirrorComponentRemoved(string kind)
    {
        _inspectedSceneNode?.Components.RemoveAll(
            c => string.Equals(c.Kind, kind, StringComparison.Ordinal));
    }

    private static string ComponentDisplayName(string kind)
    {
        return kind switch
        {
            "camera" => "Camera",
            "renderable" => "Renderable",
            "subtree_from_asset" => "Subtree from asset",
            "render_program" => "Render program",
            "proximity" => "Proximity",
            "collision" => "Collision",
            "motion" => "Motion",
            "motion_filter" => "Motion Filter",
            "audio_source" => "Audio Source",
            "audio_listener" => "Audio Listener",
            "atmosphere" => "Atmosphere",
            _ => kind,
        };
    }

    // Author the preferred asset-graph renderable: parse the asset-graph node id
    // and push it live (the engine clears the field at 0; we require non-zero to
    // "set"). Targets renderable_asset_node_id only, never the legacy slot.
    private void ApplyRenderable()
    {
        if (!EnsureCanApply())
        {
            return;
        }

        var text = RenderableAssetGraphNodeId?.Trim() ?? string.Empty;
        if (!ulong.TryParse(
                text,
                NumberStyles.Integer,
                CultureInfo.InvariantCulture,
                out var id)
            || id == 0)
        {
            LastEditError =
                "Enter a non-zero asset-graph node id for the renderable.";
            return;
        }

        var response = _editorSession!.SetNodeRenderableAsset(NodeId, id);
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }

        // Mirror the new reference onto the cached tree-node VM so re-selecting the
        // node (which repopulates the inspector from that VM) keeps the renderable
        // instead of reverting to the startup snapshot (mirrors the Visible fix).
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.Renderable =
                new EngineSceneRenderable { AssetGraphNodeId = id };
        }
    }

    private void RemoveRenderable()
    {
        if (!EnsureCanApply())
        {
            return;
        }

        var response = _editorSession!.SetNodeRenderableAsset(NodeId, 0);
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }
        HasRenderableReference = false;
        RenderableAssetGraphNodeId = string.Empty;

        // Clear the cached tree-node VM too, or re-selecting the node would read the
        // stale snapshot renderable and bring the section back (the reported bug).
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.Renderable = null;
        }
    }

    // ─── Subtree from asset (issue #213 piece 2) ─────────────────────────────────

    // Replace the picker's options with the current "Scene from GLB" asset-graph
    // nodes (threaded in from MainWindowViewModel). Preserves the active selection
    // by id when the same node is still offered so re-inspecting a node doesn't drop
    // the user's pick; otherwise resets the picker (the reference label is left as
    // the session's optimistic record).
    public void SetAvailableSceneSources(
        IEnumerable<InspectorSceneSourceOptionViewModel> options)
    {
        var previousId = _selectedSceneSourceOption?.Id;
        AvailableSceneSources.Clear();
        InspectorSceneSourceOptionViewModel? restored = null;
        foreach (var option in options)
        {
            AvailableSceneSources.Add(option);
            if (previousId is { } id && option.Id == id)
            {
                restored = option;
            }
        }
        // Assign through the field (not the property) so swapping the option
        // instances doesn't fight the ComboBox's own SelectedItem update.
        _selectedSceneSourceOption = restored;
        OnPropertyChanged(nameof(SelectedSceneSourceOption));
        OnPropertyChanged(nameof(HasAvailableSceneSources));
    }

    // Point the node at the picked "Scene from GLB" node as an Instance subtree
    // source — invoked from the picker's selection setter, so choosing a node applies
    // immediately (no separate button). Grafts the GLB hierarchy under the node; the
    // scene-tree merge (via SceneSourceChanged) then shows it.
    private void ApplySceneSource()
    {
        if (!EnsureCanApply() || SelectedSceneSourceOption is not { } option)
        {
            return;
        }

        var response = _editorSession!.SetNodeSceneSource(
            NodeId, option.Id, consumeMode: 0u);   // 0 = WZ_SCENE_SOURCE_INSTANCE
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }
        SubtreeReferenceLabel = option.Label;
        // Mirror onto the cached tree-node VM so reselect re-reveals + re-selects.
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.SceneSourceNodeId = option.Id;
        }
        SceneSourceChanged?.Invoke();
    }

    // Remove the "Subtree from asset" component (the section's ✕), mirroring how the
    // camera ✕ removes the camera: clear the engine reference (so the runtime drops
    // the grafted subtree — the scene-tree merge removes it via SceneSourceChanged)
    // and hide the section. Re-attach via "Add Component → Subtree from asset".
    private void RemoveSubtreeComponent()
    {
        if (EnsureCanApply())
        {
            // Asset-graph node id 0 clears the reference on the engine side.
            var response = _editorSession!.SetNodeSceneSource(
                NodeId, assetGraphNodeId: 0u, consumeMode: 0u);
            SetEditResponse(response);
            if (response.Ok)
            {
                SceneSourceChanged?.Invoke();
            }
        }

        // Clear the cached tree-node VM too, so reselect keeps it removed (the node
        // no longer references a subtree source).
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.SceneSourceNodeId = null;
        }

        SelectedSceneSourceOption = null;
        SubtreeReferenceLabel = string.Empty;
        HasSubtreeSection = false;
    }

    // Reveal + pre-select the "Subtree from asset" section from the node's persisted
    // scene-source ref (issue #213), or hide it when the node has none. The option
    // field is assigned directly (not the setter) so revealing never re-applies.
    private void RestoreSubtreeReferenceState(SceneTreeNodeViewModel node)
    {
        if (node.SceneSourceNodeId is { } id)
        {
            var option = AvailableSceneSources.FirstOrDefault(o => o.Id == id);
            _selectedSceneSourceOption = option;
            OnPropertyChanged(nameof(SelectedSceneSourceOption));
            SubtreeReferenceLabel = option?.Label
                ?? $"#{id.ToString(CultureInfo.InvariantCulture)}";
            HasSubtreeSection = true;
        }
        else
        {
            _selectedSceneSourceOption = null;
            OnPropertyChanged(nameof(SelectedSceneSourceOption));
            SubtreeReferenceLabel = string.Empty;
            HasSubtreeSection = false;
        }
    }

    // ─── Render program (issue #213) ─────────────────────────────────────────────

    // Replace the render-program picker's options with the current RenderProgram
    // asset-graph nodes (threaded in from MainWindowViewModel). Preserves the active
    // selection by id when the same node is still offered so re-inspecting a node
    // doesn't drop the pick; the field (not the setter) is assigned so the restore
    // never re-applies.
    public void SetAvailableRenderPrograms(
        IEnumerable<InspectorAssetGraphRefOptionViewModel> options)
    {
        var previousId = _selectedRenderProgramOption?.Id;
        AvailableRenderPrograms.Clear();
        InspectorAssetGraphRefOptionViewModel? restored = null;
        foreach (var option in options)
        {
            AvailableRenderPrograms.Add(option);
            if (previousId is { } id && option.Id == id)
            {
                restored = option;
            }
        }
        _selectedRenderProgramOption = restored;
        OnPropertyChanged(nameof(SelectedRenderProgramOption));
        OnPropertyChanged(nameof(HasAvailableRenderPrograms));
    }

    // Author the node's render program from the picked node — invoked from the
    // picker's selection setter, so choosing a node applies immediately. The program
    // is inherited by descendants without their own, so setting it on a group host
    // (e.g. a grafted GLB subtree's root) cascades one program over the whole subtree
    // and the engine applies it to each part's geometry.
    private void ApplyRenderProgram()
    {
        if (!EnsureCanApply() || SelectedRenderProgramOption is not { } option)
        {
            return;
        }

        var response = _editorSession!.SetNodeRenderProgram(NodeId, option.Id);
        SetEditResponse(response);
        if (response.Ok)
        {
            RenderProgramReferenceLabel = option.Label;
            if (_inspectedSceneNode is not null)
            {
                _inspectedSceneNode.RenderProgramNodeId = option.Id;
            }
        }
    }

    // Remove the "Render program" component (the section's ✕), mirroring the camera
    // ✕: clear the program on the engine side (id 0) and hide the section. Re-attach
    // via "Add Component → Render program".
    private void RemoveRenderProgramComponent()
    {
        if (EnsureCanApply())
        {
            SetEditResponse(_editorSession!.SetNodeRenderProgram(NodeId, 0));
        }

        // Clear the cached tree-node VM too, so reselect keeps the program removed.
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.RenderProgramNodeId = null;
        }

        ResetRenderProgramState();
    }

    // Clear the render-program picker selection + optimistic label and re-hide the
    // section. Used by the ✕ remove path (the cached VM is cleared separately there).
    private void ResetRenderProgramState()
    {
        SelectedRenderProgramOption = null;
        RenderProgramReferenceLabel = string.Empty;
        HasRenderProgramSection = false;
    }

    // Reveal + pre-select the "Render program" section from the node's persisted
    // render-program ref (issue #213), or hide it when the node has none. The option
    // field is assigned directly (not the setter) so revealing never re-applies.
    private void RestoreRenderProgramState(SceneTreeNodeViewModel node)
    {
        if (node.RenderProgramNodeId is { } programId)
        {
            var option = AvailableRenderPrograms.FirstOrDefault(
                o => o.Id == programId);
            _selectedRenderProgramOption = option;
            RenderProgramReferenceLabel = option?.Label
                ?? $"#{programId.ToString(CultureInfo.InvariantCulture)}";
            HasRenderProgramSection = true;
        }
        else
        {
            _selectedRenderProgramOption = null;
            RenderProgramReferenceLabel = string.Empty;
            HasRenderProgramSection = false;
        }
        OnPropertyChanged(nameof(SelectedRenderProgramOption));
    }

    // ─── Geometry (issue #213 increment 2) ───────────────────────────────────────

    // Replace the geometry picker's options with the current Mesh-producing
    // asset-graph nodes (threaded in from MainWindowViewModel). Same
    // selection-preserving shape as SetAvailableRenderPrograms.
    public void SetAvailableGeometrySources(
        IEnumerable<InspectorAssetGraphRefOptionViewModel> options)
    {
        var previousId = _selectedGeometryOption?.Id;
        AvailableGeometrySources.Clear();
        InspectorAssetGraphRefOptionViewModel? restored = null;
        foreach (var option in options)
        {
            AvailableGeometrySources.Add(option);
            if (previousId is { } id && option.Id == id)
            {
                restored = option;
            }
        }
        _selectedGeometryOption = restored;
        OnPropertyChanged(nameof(SelectedGeometryOption));
        OnPropertyChanged(nameof(HasAvailableGeometrySources));
    }

    // Author the node's geometry from the picked mesh node — invoked from the
    // picker's selection setter, so choosing a node applies immediately. Pairs with
    // the render program (its own section above; inherited down the tree when this
    // node doesn't carry one), completing the mesh+program binding on one node with
    // no GLB subtree graft.
    private void ApplyGeometry()
    {
        if (!EnsureCanApply() || SelectedGeometryOption is not { } option)
        {
            return;
        }

        var response = _editorSession!.SetNodeGeometryAsset(NodeId, option.Id);
        SetEditResponse(response);
        if (response.Ok)
        {
            GeometryReferenceLabel = option.Label;
            if (_inspectedSceneNode is not null)
            {
                _inspectedSceneNode.GeometryNodeId = option.Id;
            }
        }
    }

    // Remove the "Geometry" component (the section's ✕), mirroring the render
    // program ✕: clear the geometry on the engine side (id 0, the node stops
    // drawing) and hide the section. Re-attach via "Add Component → Geometry".
    private void RemoveGeometryComponent()
    {
        if (EnsureCanApply())
        {
            SetEditResponse(_editorSession!.SetNodeGeometryAsset(NodeId, 0));
        }

        // Clear the cached tree-node VM too, so reselect keeps the geometry removed.
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.GeometryNodeId = null;
        }

        ResetGeometryState();
    }

    // Clear the geometry picker selection + optimistic label and re-hide the
    // section. Used by the ✕ remove path (the cached VM is cleared separately there).
    private void ResetGeometryState()
    {
        SelectedGeometryOption = null;
        GeometryReferenceLabel = string.Empty;
        HasGeometrySection = false;
    }

    // Reveal + pre-select the "Geometry" section from the node's persisted geometry
    // ref (issue #213), or hide it when the node has none. The option field is
    // assigned directly (not the setter) so revealing never re-applies.
    private void RestoreGeometryState(SceneTreeNodeViewModel node)
    {
        if (node.GeometryNodeId is { } geometryId)
        {
            var option = AvailableGeometrySources.FirstOrDefault(
                o => o.Id == geometryId);
            _selectedGeometryOption = option;
            GeometryReferenceLabel = option?.Label
                ?? $"#{geometryId.ToString(CultureInfo.InvariantCulture)}";
            HasGeometrySection = true;
        }
        else
        {
            _selectedGeometryOption = null;
            GeometryReferenceLabel = string.Empty;
            HasGeometrySection = false;
        }
        OnPropertyChanged(nameof(SelectedGeometryOption));
    }

    // ─── Collision (terrain-stick track) ─────────────────────────────────────────

    // Replace the collision picker's options with the current Collision asset-graph
    // nodes (threaded in from MainWindowViewModel). Preserves the active selection by
    // id when the same node is still offered so re-inspecting a node doesn't drop the
    // pick; the field (not the setter) is assigned so the restore never re-applies.
    public void SetAvailableCollisionSources(
        IEnumerable<InspectorAssetGraphRefOptionViewModel> options)
    {
        var previousId = _selectedCollisionOption?.Id;
        AvailableCollisionSources.Clear();
        InspectorAssetGraphRefOptionViewModel? restored = null;
        foreach (var option in options)
        {
            AvailableCollisionSources.Add(option);
            if (previousId is { } id && option.Id == id)
            {
                restored = option;
            }
        }
        _selectedCollisionOption = restored;
        OnPropertyChanged(nameof(SelectedCollisionOption));
        OnPropertyChanged(nameof(HasAvailableCollisionSources));
    }

    // Apply the collision reference from the picked node — invoked from the picker's
    // selection setter, so choosing a node applies immediately. Pushes the chosen
    // asset-graph node id + the current constrain-movement flag.
    private void ApplyCollision()
    {
        if (!EnsureCanApply() || SelectedCollisionOption is not { } option)
        {
            return;
        }

        var response = _editorSession!.SetNodeCollision(
            NodeId, AssetGraphNodeIdAsUint(option.Id), CollisionConstrainMovement);
        SetEditResponse(response);
        if (response.Ok)
        {
            CollisionReferenceLabel = option.Label;
        }
        MirrorCollisionEdit();
    }

    // Re-push the collision binding when the constrain-movement flag toggles, with
    // the current selection (or 0 when nothing is picked). Suppressed while a node's
    // values are being loaded into the fields so selecting a node doesn't echo back.
    private void OnCollisionFieldEdited()
    {
        if (_suppressLiveEdits || !EnsureCanApply())
        {
            return;
        }

        var assetId = SelectedCollisionOption is { } option
            ? AssetGraphNodeIdAsUint(option.Id)
            : 0u;
        SetEditResponse(_editorSession!.SetNodeCollision(
            NodeId, assetId, CollisionConstrainMovement));
        MirrorCollisionEdit();
    }

    // Mirror the live collision edit onto the cached tree-node VM (visibility-
    // revert fix pattern, f2da3c7) so an immediate reselect — before the next
    // snapshot refresh — shows the edit instead of reverting to the snapshot.
    private void MirrorCollisionEdit()
    {
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.Collision = new EngineSceneNodeCollision
            {
                CollisionAssetNodeId = SelectedCollisionOption?.Id,
                ConstrainMovement = CollisionConstrainMovement,
            };
        }
    }

    // Remove the Collision component (the section's ✕), mirroring the camera ✕:
    // remove it on the engine via the generic verb and hide the section. Re-attach
    // via "Add Component → Collision".
    private void RemoveCollisionComponent()
    {
        if (EnsureCanApply())
        {
            var response = _editorSession!.RemoveNodeComponent(NodeId, "collision");
            SetEditResponse(response);
        }

        MirrorComponentRemoved("collision");
        ResetCollisionState();
    }

    // Reveal the "Collision" section when the node carries the component and
    // restore its persisted field values from the snapshot (read-back gap fix):
    // pre-select the referenced collision asset option (matching by id, like
    // RestoreRenderProgramState) and the constrain-movement flag. Runs under
    // _suppressLiveEdits so populating the fields doesn't echo a live edit.
    private void RestoreCollisionState(SceneTreeNodeViewModel node)
    {
        var has = node.Components.Any(
            c => string.Equals(c.Kind, "collision", StringComparison.Ordinal));
        if (!has)
        {
            ResetCollisionState();
            return;
        }

        HasCollisionComponent = true;

        var collision = node.Collision;
        if (collision?.CollisionAssetNodeId is { } assetNodeId)
        {
            var option = AvailableCollisionSources.FirstOrDefault(
                o => o.Id == assetNodeId);
            _selectedCollisionOption = option;
            CollisionReferenceLabel = option?.Label
                ?? $"#{assetNodeId.ToString(CultureInfo.InvariantCulture)}";
        }
        else
        {
            _selectedCollisionOption = null;
            CollisionReferenceLabel = string.Empty;
        }
        OnPropertyChanged(nameof(SelectedCollisionOption));

        _collisionConstrainMovement = collision?.ConstrainMovement ?? false;
        OnPropertyChanged(nameof(CollisionConstrainMovement));
    }

    private void ResetCollisionState()
    {
        _selectedCollisionOption = null;
        OnPropertyChanged(nameof(SelectedCollisionOption));
        CollisionReferenceLabel = string.Empty;
        // Reset the flag without echoing a live edit.
        _collisionConstrainMovement = false;
        OnPropertyChanged(nameof(CollisionConstrainMovement));
        HasCollisionComponent = false;
    }

    // ─── Atmosphere ──────────────────────────────────────────────────────────────

    // Replace the atmosphere picker's options with the current Atmosphere asset-
    // graph nodes (threaded in from MainWindowViewModel). Preserves the active
    // selection by id; the field (not the setter) is assigned so restore never
    // re-applies.
    public void SetAvailableAtmospheres(
        IEnumerable<InspectorAssetGraphRefOptionViewModel> options)
    {
        var previousId = _selectedAtmosphereOption?.Id;
        AvailableAtmospheres.Clear();
        InspectorAssetGraphRefOptionViewModel? restored = null;
        foreach (var option in options)
        {
            AvailableAtmospheres.Add(option);
            if (previousId is { } id && option.Id == id)
            {
                restored = option;
            }
        }
        _selectedAtmosphereOption = restored;
        OnPropertyChanged(nameof(SelectedAtmosphereOption));
        OnPropertyChanged(nameof(HasAvailableAtmospheres));
    }

    // Apply the atmosphere reference from the picked node — invoked from the
    // picker's selection setter, so choosing a node applies immediately. Pushes the
    // chosen node id + the current enabled flag (one combined seam). The seam takes
    // a ulong, so the id is passed straight through (no uint clamp).
    private void ApplyAtmosphere()
    {
        if (!EnsureCanApply() || SelectedAtmosphereOption is not { } option)
        {
            return;
        }

        var response = _editorSession!.SetNodeAtmosphere(
            NodeId, option.Id, AtmosphereEnabled);
        SetEditResponse(response);
        if (response.Ok)
        {
            AtmosphereReferenceLabel = option.Label;
        }
        MirrorAtmosphereEdit();
    }

    // Re-push the atmosphere binding when the enabled flag toggles, with the
    // current selection (or 0 when nothing is picked). Suppressed while a node's
    // values are being loaded so selecting a node doesn't echo back.
    private void OnAtmosphereFieldEdited()
    {
        if (_suppressLiveEdits || !EnsureCanApply())
        {
            return;
        }

        var assetId = SelectedAtmosphereOption is { } option ? option.Id : 0ul;
        SetEditResponse(_editorSession!.SetNodeAtmosphere(
            NodeId, assetId, AtmosphereEnabled));
        MirrorAtmosphereEdit();
    }

    // Mirror the live atmosphere edit onto the cached tree-node VM so an immediate
    // reselect — before the next snapshot refresh — shows the edit.
    private void MirrorAtmosphereEdit()
    {
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.Atmosphere = new EngineSceneNodeAtmosphere
            {
                AtmosphereAssetNodeId = SelectedAtmosphereOption?.Id,
                Enabled = AtmosphereEnabled,
            };
        }
    }

    // Remove the Atmosphere component (the section's ✕): remove it on the engine
    // via the generic verb and hide the section. Re-attach via "Add Component →
    // Atmosphere".
    private void RemoveAtmosphereComponent()
    {
        if (EnsureCanApply())
        {
            var response = _editorSession!.RemoveNodeComponent(NodeId, "atmosphere");
            SetEditResponse(response);
        }

        MirrorComponentRemoved("atmosphere");
        ResetAtmosphereState();
    }

    // Reveal the "Atmosphere" section when the node carries the component and
    // restore its persisted field values from the snapshot: pre-select the
    // referenced Atmosphere asset (matching by id) and the enabled flag. Runs under
    // _suppressLiveEdits so populating the fields doesn't echo a live edit.
    private void RestoreAtmosphereState(SceneTreeNodeViewModel node)
    {
        var has = node.Components.Any(
            c => string.Equals(c.Kind, "atmosphere", StringComparison.Ordinal));
        if (!has)
        {
            ResetAtmosphereState();
            return;
        }

        HasAtmosphereComponent = true;

        var atmosphere = node.Atmosphere;
        if (atmosphere?.AtmosphereAssetNodeId is { } assetNodeId)
        {
            var option = AvailableAtmospheres.FirstOrDefault(
                o => o.Id == assetNodeId);
            _selectedAtmosphereOption = option;
            AtmosphereReferenceLabel = option?.Label
                ?? $"#{assetNodeId.ToString(CultureInfo.InvariantCulture)}";
        }
        else
        {
            _selectedAtmosphereOption = null;
            AtmosphereReferenceLabel = string.Empty;
        }
        OnPropertyChanged(nameof(SelectedAtmosphereOption));

        _atmosphereEnabled = atmosphere?.Enabled ?? true;
        OnPropertyChanged(nameof(AtmosphereEnabled));
    }

    private void ResetAtmosphereState()
    {
        _selectedAtmosphereOption = null;
        OnPropertyChanged(nameof(SelectedAtmosphereOption));
        AtmosphereReferenceLabel = string.Empty;
        // Reset the flag without echoing a live edit.
        _atmosphereEnabled = true;
        OnPropertyChanged(nameof(AtmosphereEnabled));
        HasAtmosphereComponent = false;
    }

    // ─── Environment ─────────────────────────────────────────────────────────────

    // Thread in the FrameEnvironment asset-graph nodes the picker offers, restoring
    // the prior selection by id (mirrors SetAvailableAtmospheres).
    public void SetAvailableEnvironments(
        IEnumerable<InspectorAssetGraphRefOptionViewModel> options)
    {
        var previousId = _selectedEnvironmentOption?.Id;
        AvailableEnvironments.Clear();
        InspectorAssetGraphRefOptionViewModel? restored = null;
        foreach (var option in options)
        {
            AvailableEnvironments.Add(option);
            if (previousId is { } id && option.Id == id)
            {
                restored = option;
            }
        }
        _selectedEnvironmentOption = restored;
        OnPropertyChanged(nameof(SelectedEnvironmentOption));
        OnPropertyChanged(nameof(HasAvailableEnvironments));
    }

    // Apply the environment reference from the picked node — invoked from the
    // picker's selection setter, so choosing a node applies immediately.
    private void ApplyEnvironment()
    {
        if (!EnsureCanApply() || SelectedEnvironmentOption is not { } option)
        {
            return;
        }

        var response = _editorSession!.SetNodeEnvironment(
            NodeId, option.Id, EnvironmentEnabled);
        SetEditResponse(response);
        if (response.Ok)
        {
            EnvironmentReferenceLabel = option.Label;
        }
        MirrorEnvironmentEdit();
    }

    // Re-push the environment binding when the enabled flag toggles, with the
    // current selection (or 0 when nothing is picked). Suppressed while a node's
    // values are being loaded so selecting a node doesn't echo back.
    private void OnEnvironmentFieldEdited()
    {
        if (_suppressLiveEdits || !EnsureCanApply())
        {
            return;
        }

        var assetId = SelectedEnvironmentOption is { } option ? option.Id : 0ul;
        SetEditResponse(_editorSession!.SetNodeEnvironment(
            NodeId, assetId, EnvironmentEnabled));
        MirrorEnvironmentEdit();
    }

    // Mirror the live environment edit onto the cached tree-node VM so an immediate
    // reselect — before the next snapshot refresh — shows the edit.
    private void MirrorEnvironmentEdit()
    {
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.Environment = new EngineSceneNodeEnvironment
            {
                EnvironmentAssetNodeId = SelectedEnvironmentOption?.Id,
                Enabled = EnvironmentEnabled,
            };
        }
    }

    // Remove the Environment component (the section's ✕): remove it on the engine
    // via the generic verb and hide the section. Re-attach via "Add Component →
    // Environment".
    private void RemoveEnvironmentComponent()
    {
        if (EnsureCanApply())
        {
            var response = _editorSession!.RemoveNodeComponent(NodeId, "environment");
            SetEditResponse(response);
        }

        MirrorComponentRemoved("environment");
        ResetEnvironmentState();
    }

    // Reveal the "Environment" section when the node carries the component and
    // restore its persisted field values from the snapshot: pre-select the
    // referenced FrameEnvironment asset (matching by id) and the enabled flag. Runs
    // under _suppressLiveEdits so populating the fields doesn't echo a live edit.
    private void RestoreEnvironmentState(SceneTreeNodeViewModel node)
    {
        var has = node.Components.Any(
            c => string.Equals(c.Kind, "environment", StringComparison.Ordinal));
        if (!has)
        {
            ResetEnvironmentState();
            return;
        }

        HasEnvironmentComponent = true;

        var environment = node.Environment;
        if (environment?.EnvironmentAssetNodeId is { } assetNodeId)
        {
            var option = AvailableEnvironments.FirstOrDefault(
                o => o.Id == assetNodeId);
            _selectedEnvironmentOption = option;
            EnvironmentReferenceLabel = option?.Label
                ?? $"#{assetNodeId.ToString(CultureInfo.InvariantCulture)}";
        }
        else
        {
            _selectedEnvironmentOption = null;
            EnvironmentReferenceLabel = string.Empty;
        }
        OnPropertyChanged(nameof(SelectedEnvironmentOption));

        _environmentEnabled = environment?.Enabled ?? true;
        OnPropertyChanged(nameof(EnvironmentEnabled));
    }

    private void ResetEnvironmentState()
    {
        _selectedEnvironmentOption = null;
        OnPropertyChanged(nameof(SelectedEnvironmentOption));
        EnvironmentReferenceLabel = string.Empty;
        // Reset the flag without echoing a live edit.
        _environmentEnabled = true;
        OnPropertyChanged(nameof(EnvironmentEnabled));
        HasEnvironmentComponent = false;
    }

    // ─── Audio Source (audio-track item 10) ──────────────────────────────────────

    // Thread in the audio-renderable asset-graph nodes the picker offers, restoring
    // the prior selection by id (mirrors SetAvailableCollisionSources).
    public void SetAvailableAudioRenderables(
        IEnumerable<InspectorAssetGraphRefOptionViewModel> options)
    {
        var previousId = _selectedAudioRenderableOption?.Id;
        AvailableAudioRenderables.Clear();
        InspectorAssetGraphRefOptionViewModel? restored = null;
        foreach (var option in options)
        {
            AvailableAudioRenderables.Add(option);
            if (previousId is { } id && option.Id == id)
            {
                restored = option;
            }
        }
        _selectedAudioRenderableOption = restored;
        OnPropertyChanged(nameof(SelectedAudioRenderableOption));
        OnPropertyChanged(nameof(HasAvailableAudioRenderables));
    }

    // Apply the picked audio-renderable reference (stores the stable node id).
    private void ApplyAudioRenderable()
    {
        if (!EnsureCanApply() || SelectedAudioRenderableOption is not { } option)
        {
            return;
        }

        var response = _editorSession!.SetNodeAudioRenderable(NodeId, option.Id);
        SetEditResponse(response);
        if (response.Ok)
        {
            AudioRenderableReferenceLabel = option.Label;
        }
        MirrorAudioSourceEdit();
    }

    // Re-push the play policy when auto_play / enabled toggles. Suppressed while a
    // node's values are loading so selecting a node doesn't echo back.
    private void OnAudioSourcePlayEdited()
    {
        if (_suppressLiveEdits || !EnsureCanApply())
        {
            return;
        }
        SetEditResponse(_editorSession!.SetNodeAudioSourcePlay(
            NodeId, AudioSourceAutoPlay, AudioSourceEnabled));
        MirrorAudioSourceEdit();
    }

    // Mirror the live edit onto the cached tree-node VM so an immediate reselect
    // shows the edit instead of reverting to the snapshot (collision pattern).
    private void MirrorAudioSourceEdit()
    {
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.AudioSource = new EngineSceneNodeAudioSource
            {
                AudioRenderableNodeId = SelectedAudioRenderableOption?.Id,
                AutoPlay = AudioSourceAutoPlay,
                Enabled = AudioSourceEnabled,
            };
        }
    }

    // Remove the AudioSource component (section ✕): generic remove verb + hide.
    private void RemoveAudioSourceComponent()
    {
        if (EnsureCanApply())
        {
            var response =
                _editorSession!.RemoveNodeComponent(NodeId, "audio_source");
            SetEditResponse(response);
        }

        MirrorComponentRemoved("audio_source");
        ResetAudioSourceState();
    }

    // Reveal the section when the node carries an audio_source component and restore
    // its persisted fields from the snapshot (pre-select the referenced renderable
    // by id + the play flags). Runs under _suppressLiveEdits so populating doesn't
    // echo a live edit.
    private void RestoreAudioSourceState(SceneTreeNodeViewModel node)
    {
        var has = node.Components.Any(
            c => string.Equals(c.Kind, "audio_source", StringComparison.Ordinal));
        if (!has)
        {
            ResetAudioSourceState();
            return;
        }

        HasAudioSourceComponent = true;

        var audio = node.AudioSource;
        if (audio?.AudioRenderableNodeId is { } nodeId)
        {
            var option = AvailableAudioRenderables.FirstOrDefault(
                o => o.Id == nodeId);
            _selectedAudioRenderableOption = option;
            AudioRenderableReferenceLabel = option?.Label
                ?? $"#{nodeId.ToString(CultureInfo.InvariantCulture)}";
        }
        else
        {
            _selectedAudioRenderableOption = null;
            AudioRenderableReferenceLabel = string.Empty;
        }
        OnPropertyChanged(nameof(SelectedAudioRenderableOption));

        _audioSourceAutoPlay = audio?.AutoPlay ?? true;
        OnPropertyChanged(nameof(AudioSourceAutoPlay));
        _audioSourceEnabled = audio?.Enabled ?? true;
        OnPropertyChanged(nameof(AudioSourceEnabled));
    }

    private void ResetAudioSourceState()
    {
        _selectedAudioRenderableOption = null;
        OnPropertyChanged(nameof(SelectedAudioRenderableOption));
        AudioRenderableReferenceLabel = string.Empty;
        _audioSourceAutoPlay = true;
        OnPropertyChanged(nameof(AudioSourceAutoPlay));
        _audioSourceEnabled = true;
        OnPropertyChanged(nameof(AudioSourceEnabled));
        HasAudioSourceComponent = false;
    }

    // ─── Motion (terrain-stick track) ────────────────────────────────────────────

    // Re-push the motion terrain-constraint fields on any change. Suppressed while a
    // node's values are being loaded into the fields; a mid-edit / unparseable
    // numeric field is treated as 0 so typing doesn't error (mirrors the live
    // transform's lenient parse).
    private void OnMotionFieldEdited()
    {
        if (_suppressLiveEdits || !EnsureCanApply())
        {
            return;
        }

        SetEditResponse(_editorSession!.SetNodeMotionTerrain(
            NodeId,
            MotionTerrainConstrained,
            ParseFloatOrZero(MotionRideHeight),
            ParseFloatOrZero(MotionFootprintRadius),
            MotionAlignToSurface,
            ParseFloatOrZero(MotionAlignmentStrength)));

        // Mirror onto the cached tree-node VM (visibility-revert fix pattern,
        // f2da3c7) so an immediate reselect shows the edit, not the stale snapshot.
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.Motion = new EngineSceneNodeMotion
            {
                TerrainConstrained = MotionTerrainConstrained,
                RideHeight = ParseFloatOrZero(MotionRideHeight),
                FootprintRadius = ParseFloatOrZero(MotionFootprintRadius),
                AlignToSurface = MotionAlignToSurface,
                AlignmentStrength = ParseFloatOrZero(MotionAlignmentStrength),
            };
        }
    }

    // Remove the Motion component (the section's ✕), mirroring the camera ✕.
    private void RemoveMotionComponent()
    {
        if (EnsureCanApply())
        {
            var response = _editorSession!.RemoveNodeComponent(NodeId, "motion");
            SetEditResponse(response);
        }

        MirrorComponentRemoved("motion");
        ResetMotionState();
    }

    // Reveal the "Motion" section when the node carries the component and restore
    // its persisted terrain-stick field values from the snapshot (read-back gap
    // fix). Runs under _suppressLiveEdits so populating doesn't echo a live edit.
    private void RestoreMotionState(SceneTreeNodeViewModel node)
    {
        var has = node.Components.Any(
            c => string.Equals(c.Kind, "motion", StringComparison.Ordinal));
        if (!has)
        {
            ResetMotionState();
            return;
        }

        HasMotionComponent = true;

        var motion = node.Motion;
        _motionTerrainConstrained = motion?.TerrainConstrained ?? false;
        OnPropertyChanged(nameof(MotionTerrainConstrained));
        _motionRideHeight = motion is not null
            ? FormatFloat(motion.RideHeight)
            : string.Empty;
        OnPropertyChanged(nameof(MotionRideHeight));
        _motionFootprintRadius = motion is not null
            ? FormatFloat(motion.FootprintRadius)
            : string.Empty;
        OnPropertyChanged(nameof(MotionFootprintRadius));
        _motionAlignToSurface = motion?.AlignToSurface ?? false;
        OnPropertyChanged(nameof(MotionAlignToSurface));
        _motionAlignmentStrength = motion is not null
            ? FormatFloat(motion.AlignmentStrength)
            : string.Empty;
        OnPropertyChanged(nameof(MotionAlignmentStrength));
    }

    private static string FormatFloat(float value) =>
        value.ToString("0.###", CultureInfo.InvariantCulture);

    private void ResetMotionState()
    {
        // Reset every field without echoing live edits.
        _motionTerrainConstrained = false;
        OnPropertyChanged(nameof(MotionTerrainConstrained));
        _motionRideHeight = string.Empty;
        OnPropertyChanged(nameof(MotionRideHeight));
        _motionFootprintRadius = string.Empty;
        OnPropertyChanged(nameof(MotionFootprintRadius));
        _motionAlignToSurface = false;
        OnPropertyChanged(nameof(MotionAlignToSurface));
        _motionAlignmentStrength = string.Empty;
        OnPropertyChanged(nameof(MotionAlignmentStrength));
        HasMotionComponent = false;
    }

    // Push the whole Motion Filter component live on any field change (the engine
    // sends it all + patches the live record in place), and mirror onto the cached
    // tree-node VM so an immediate reselect shows the edit, not the stale snapshot.
    private void OnMotionFilterFieldEdited()
    {
        if (_suppressLiveEdits || !EnsureCanApply())
        {
            return;
        }

        var filter = BuildMotionFilterFromFields();
        SetEditResponse(_editorSession!.SetNodeMotionFilter(NodeId, filter));

        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.MotionFilter = filter;
        }
    }

    private EngineSceneNodeMotionFilter BuildMotionFilterFromFields()
    {
        EngineSceneNodeMotionFilterRotationAxis Axis(
            string smoothing, bool level, bool limit, string min, string max) =>
            new()
            {
                SmoothingTime = ParseFloatOrZero(smoothing),
                Level = level,
                Limit = limit,
                LimitMinDegrees = ParseFloatOrZero(min),
                LimitMaxDegrees = ParseFloatOrZero(max),
            };

        return new EngineSceneNodeMotionFilter
        {
            TranslationSmoothing =
            [
                ParseFloatOrZero(MotionFilterTranslationSmoothingX),
                ParseFloatOrZero(MotionFilterTranslationSmoothingY),
                ParseFloatOrZero(MotionFilterTranslationSmoothingZ),
            ],
            TerrainFloor = MotionFilterTerrainFloor,
            TerrainFloorOffset = ParseFloatOrZero(MotionFilterTerrainFloorOffset),
            Roll = Axis(
                MotionFilterRollSmoothing, MotionFilterRollLevel,
                MotionFilterRollLimit, MotionFilterRollLimitMin,
                MotionFilterRollLimitMax),
            Pitch = Axis(
                MotionFilterPitchSmoothing, MotionFilterPitchLevel,
                MotionFilterPitchLimit, MotionFilterPitchLimitMin,
                MotionFilterPitchLimitMax),
            Yaw = Axis(
                MotionFilterYawSmoothing, MotionFilterYawLevel,
                MotionFilterYawLimit, MotionFilterYawLimitMin,
                MotionFilterYawLimitMax),
            Enabled = MotionFilterEnabled,
        };
    }

    // Remove the Motion Filter component (the section's ✕), mirroring Motion.
    private void RemoveMotionFilterComponent()
    {
        if (EnsureCanApply())
        {
            SetEditResponse(
                _editorSession!.RemoveNodeComponent(NodeId, "motion_filter"));
        }

        MirrorComponentRemoved("motion_filter");
        ResetMotionFilterState();
    }

    // Reveal the "Motion Filter" section + restore its persisted fields from the
    // snapshot. Runs under _suppressLiveEdits so populating doesn't echo an edit.
    private void RestoreMotionFilterState(SceneTreeNodeViewModel node)
    {
        var has = node.Components.Any(
            c => string.Equals(
                c.Kind, "motion_filter", StringComparison.Ordinal));
        if (!has)
        {
            ResetMotionFilterState();
            return;
        }

        HasMotionFilterComponent = true;

        var f = node.MotionFilter;
        var t = f?.TranslationSmoothing;

        _motionFilterEnabled = f?.Enabled ?? true;
        OnPropertyChanged(nameof(MotionFilterEnabled));
        _motionFilterTranslationSmoothingX = FormatAxis(t, 0);
        OnPropertyChanged(nameof(MotionFilterTranslationSmoothingX));
        _motionFilterTranslationSmoothingY = FormatAxis(t, 1);
        OnPropertyChanged(nameof(MotionFilterTranslationSmoothingY));
        _motionFilterTranslationSmoothingZ = FormatAxis(t, 2);
        OnPropertyChanged(nameof(MotionFilterTranslationSmoothingZ));
        _motionFilterTerrainFloor = f?.TerrainFloor ?? false;
        OnPropertyChanged(nameof(MotionFilterTerrainFloor));
        _motionFilterTerrainFloorOffset =
            f is not null ? FormatFloat(f.TerrainFloorOffset) : string.Empty;
        OnPropertyChanged(nameof(MotionFilterTerrainFloorOffset));

        RestoreMotionFilterAxis(
            f?.Roll,
            ref _motionFilterRollSmoothing, nameof(MotionFilterRollSmoothing),
            ref _motionFilterRollLevel, nameof(MotionFilterRollLevel),
            ref _motionFilterRollLimit, nameof(MotionFilterRollLimit),
            ref _motionFilterRollLimitMin, nameof(MotionFilterRollLimitMin),
            ref _motionFilterRollLimitMax, nameof(MotionFilterRollLimitMax));
        RestoreMotionFilterAxis(
            f?.Pitch,
            ref _motionFilterPitchSmoothing, nameof(MotionFilterPitchSmoothing),
            ref _motionFilterPitchLevel, nameof(MotionFilterPitchLevel),
            ref _motionFilterPitchLimit, nameof(MotionFilterPitchLimit),
            ref _motionFilterPitchLimitMin, nameof(MotionFilterPitchLimitMin),
            ref _motionFilterPitchLimitMax, nameof(MotionFilterPitchLimitMax));
        RestoreMotionFilterAxis(
            f?.Yaw,
            ref _motionFilterYawSmoothing, nameof(MotionFilterYawSmoothing),
            ref _motionFilterYawLevel, nameof(MotionFilterYawLevel),
            ref _motionFilterYawLimit, nameof(MotionFilterYawLimit),
            ref _motionFilterYawLimitMin, nameof(MotionFilterYawLimitMin),
            ref _motionFilterYawLimitMax, nameof(MotionFilterYawLimitMax));
    }

    private void RestoreMotionFilterAxis(
        EngineSceneNodeMotionFilterRotationAxis? axis,
        ref string smoothing, string smoothingName,
        ref bool level, string levelName,
        ref bool limit, string limitName,
        ref string min, string minName,
        ref string max, string maxName)
    {
        smoothing = axis is not null ? FormatFloat(axis.SmoothingTime) : string.Empty;
        OnPropertyChanged(smoothingName);
        level = axis?.Level ?? false;
        OnPropertyChanged(levelName);
        limit = axis?.Limit ?? false;
        OnPropertyChanged(limitName);
        min = axis is not null ? FormatFloat(axis.LimitMinDegrees) : string.Empty;
        OnPropertyChanged(minName);
        max = axis is not null ? FormatFloat(axis.LimitMaxDegrees) : string.Empty;
        OnPropertyChanged(maxName);
    }

    private static string FormatAxis(float[]? values, int index) =>
        values is not null && index < values.Length
            ? FormatFloat(values[index])
            : string.Empty;

    private void ResetMotionFilterState()
    {
        _motionFilterEnabled = true;
        OnPropertyChanged(nameof(MotionFilterEnabled));
        _motionFilterTranslationSmoothingX = string.Empty;
        OnPropertyChanged(nameof(MotionFilterTranslationSmoothingX));
        _motionFilterTranslationSmoothingY = string.Empty;
        OnPropertyChanged(nameof(MotionFilterTranslationSmoothingY));
        _motionFilterTranslationSmoothingZ = string.Empty;
        OnPropertyChanged(nameof(MotionFilterTranslationSmoothingZ));
        _motionFilterTerrainFloor = false;
        OnPropertyChanged(nameof(MotionFilterTerrainFloor));
        _motionFilterTerrainFloorOffset = string.Empty;
        OnPropertyChanged(nameof(MotionFilterTerrainFloorOffset));

        RestoreMotionFilterAxis(
            null,
            ref _motionFilterRollSmoothing, nameof(MotionFilterRollSmoothing),
            ref _motionFilterRollLevel, nameof(MotionFilterRollLevel),
            ref _motionFilterRollLimit, nameof(MotionFilterRollLimit),
            ref _motionFilterRollLimitMin, nameof(MotionFilterRollLimitMin),
            ref _motionFilterRollLimitMax, nameof(MotionFilterRollLimitMax));
        RestoreMotionFilterAxis(
            null,
            ref _motionFilterPitchSmoothing, nameof(MotionFilterPitchSmoothing),
            ref _motionFilterPitchLevel, nameof(MotionFilterPitchLevel),
            ref _motionFilterPitchLimit, nameof(MotionFilterPitchLimit),
            ref _motionFilterPitchLimitMin, nameof(MotionFilterPitchLimitMin),
            ref _motionFilterPitchLimitMax, nameof(MotionFilterPitchLimitMax));
        RestoreMotionFilterAxis(
            null,
            ref _motionFilterYawSmoothing, nameof(MotionFilterYawSmoothing),
            ref _motionFilterYawLevel, nameof(MotionFilterYawLevel),
            ref _motionFilterYawLimit, nameof(MotionFilterYawLimit),
            ref _motionFilterYawLimitMin, nameof(MotionFilterYawLimitMin),
            ref _motionFilterYawLimitMax, nameof(MotionFilterYawLimitMax));

        HasMotionFilterComponent = false;
    }

    // The Collision verb takes a uint asset-graph node id; clamp the option's ulong
    // id to uint (ids are small counters, so this never loses information in
    // practice — a value past uint is treated as 0/clear).
    private static uint AssetGraphNodeIdAsUint(ulong id) =>
        id <= uint.MaxValue ? (uint)id : 0u;

    private static float ParseFloatOrZero(string text)
    {
        return float.TryParse(
            text,
            NumberStyles.Float,
            CultureInfo.InvariantCulture,
            out var value)
            ? value
            : 0f;
    }

    // ─── GLB node tree picker (issue #213) ───────────────────────────────────────

    // Thread the current asset-graph snapshot (nodes + edges) into the inspector so
    // a "Mesh from GLB scene" node can resolve its connected GLB by walking edges
    // (MainWindowViewModel calls this on every selection, mirroring how the "Scene
    // from GLB" picker options are threaded). Plain snapshot data — no dependency on
    // the asset-graph pane. If the topology changes while such a node is inspected,
    // rebuild the tree so it stays in step.
    public void SetAssetGraphTopology(
        IEnumerable<EngineAssetGraphNode> nodes,
        IEnumerable<EngineAssetGraphEdge> edges)
    {
        _assetGraphNodes = nodes as IReadOnlyList<EngineAssetGraphNode>
            ?? nodes.ToList();
        _assetGraphEdges = edges as IReadOnlyList<EngineAssetGraphEdge>
            ?? edges.ToList();
    }

    // ── Renderable bindings + constants (issue #230) ────────────────────────

    // Asset types whose compilers publish a bindable GPU resource, keyed by
    // the resource kind a layout row declares (the editor-side projection of
    // the engine's render_binding_sources.h publisher map):
    //   Texture SRV    ← ScalarField (128), VectorField (2258)
    //   Structured SRV ← GaussianSplatCloud (131), GpuSparseMesh (537)
    private static readonly uint[] TextureBindingSourceTypes = [128, 2258];
    private static readonly uint[] StructuredBindingSourceTypes = [131, 537];

    // The custom render program's optional binding-layout input port (#227).
    private const uint CustomProgramLayoutInputPort = 2;

    // Declared constant widths by the layout's constN_type ordinal
    // (Float, Float2, Float3, Float4, Color).
    private static readonly (string Label, int Width)[] ConstantTypeTable =
    [
        ("float", 1),
        ("float2", 2),
        ("float3", 3),
        ("float4", 4),
        ("color", 4),
    ];

    private void ResetRenderableIngredientsState()
    {
        RenderableBindingRows.Clear();
        RenderableConstantRows.Clear();
        HasRenderableIngredientsSection = false;
        RenderableIngredientsHint = string.Empty;
        OnPropertyChanged(nameof(HasRenderableBindingRows));
        OnPropertyChanged(nameof(HasRenderableConstantRows));
    }

    // Build the ingredient form from the node's persisted state + the wired
    // program's authored layout. Rows come from the LAYOUT (the contract),
    // pre-selected from the node's authored bindings/overrides; a node that
    // carries ingredients but whose layout cannot be resolved still reveals
    // the section with a hint (never silently hides authored state).
    private void RestoreRenderableIngredientsState(SceneTreeNodeViewModel node)
    {
        ResetRenderableIngredientsState();

        var hasAuthoredIngredients =
            node.RenderableBindings.Count > 0
            || node.RenderableConstants.Count > 0;

        var programNodeId = ResolveEffectiveRenderProgramNodeId(node);
        if (programNodeId is not { } programId)
        {
            if (hasAuthoredIngredients)
            {
                HasRenderableIngredientsSection = true;
                RenderableIngredientsHint =
                    "This node has renderable bindings/constants but no "
                    + "effective render program (own or inherited).";
            }
            return;
        }

        var layoutNodeId = EdgeSourceInto(programId, CustomProgramLayoutInputPort);
        var layoutNode = layoutNodeId is { } layoutId
            ? FindSnapshotNode(layoutId)
            : null;
        if (layoutNode is null)
        {
            if (hasAuthoredIngredients)
            {
                HasRenderableIngredientsSection = true;
                RenderableIngredientsHint =
                    "This node has renderable bindings/constants but its "
                    + "render program wires no binding layout (#227); bindings "
                    + "need a layout-authored custom program.";
            }
            return;
        }

        // One picker row per declared SRV semantic the geometry does not own
        // (pulled_mesh_* rows are satisfied by the geometry port).
        for (var i = 0; i < 8; ++i)
        {
            var semantic = LayoutParamValue(layoutNode, $"binding{i}_semantic");
            if (string.IsNullOrWhiteSpace(semantic)
                || semantic.StartsWith("pulled_mesh_", StringComparison.Ordinal))
            {
                continue;
            }

            var isStructured = IsStructuredBindingKind(
                LayoutParamValue(layoutNode, $"binding{i}_kind"));
            var options = new ObservableCollection<
                InspectorAssetGraphRefOptionViewModel>();
            foreach (var option in BindingSourceOptions(isStructured))
            {
                options.Add(option);
            }

            ulong? selected = null;
            foreach (var binding in node.RenderableBindings)
            {
                if (string.Equals(
                        binding.Semantic, semantic, StringComparison.Ordinal))
                {
                    selected = binding.AssetGraphNodeId;
                    break;
                }
            }

            RenderableBindingRows.Add(new InspectorRenderableBindingRowViewModel(
                semantic!,
                isStructured ? "buffer" : "texture",
                options,
                selected,
                ApplyRenderableBinding));
        }

        // One typed value row per declared constant tail field.
        for (var i = 0; i < 8; ++i)
        {
            var name = LayoutParamValue(layoutNode, $"const{i}_name");
            if (string.IsNullOrWhiteSpace(name))
            {
                continue;
            }

            var (typeLabel, width) = ConstantTypeOf(
                LayoutParamValue(layoutNode, $"const{i}_type"));

            float[]? initial = null;
            foreach (var constant in node.RenderableConstants)
            {
                if (string.Equals(
                        constant.Name, name, StringComparison.Ordinal))
                {
                    initial = constant.Value;
                    break;
                }
            }

            RenderableConstantRows.Add(
                new InspectorRenderableConstantRowViewModel(
                    name!,
                    width,
                    typeLabel,
                    initial,
                    ApplyRenderableParam));
        }

        if (RenderableBindingRows.Count > 0
            || RenderableConstantRows.Count > 0
            || hasAuthoredIngredients)
        {
            HasRenderableIngredientsSection = true;
        }
        OnPropertyChanged(nameof(HasRenderableBindingRows));
        OnPropertyChanged(nameof(HasRenderableConstantRows));
    }

    // The node's effective render program: its own reference, else the nearest
    // ParentId ancestor's (the same inheritance the engine's assemble walk
    // applies). Bounded + cycle-safe; needs the cross-pane node lookup.
    private ulong? ResolveEffectiveRenderProgramNodeId(
        SceneTreeNodeViewModel node)
    {
        if (node.RenderProgramNodeId is { } own)
        {
            return own;
        }
        if (_sceneNodeLookup is null)
        {
            return null;
        }

        var visited = new HashSet<string>(StringComparer.Ordinal) { node.Id };
        var parentId = node.ParentId;
        while (!string.IsNullOrEmpty(parentId) && visited.Add(parentId!))
        {
            var parent = _sceneNodeLookup(parentId!);
            if (parent is null)
            {
                return null;
            }
            if (parent.RenderProgramNodeId is { } inherited)
            {
                return inherited;
            }
            parentId = parent.ParentId;
        }
        return null;
    }

    private IEnumerable<InspectorAssetGraphRefOptionViewModel>
        BindingSourceOptions(bool isStructured)
    {
        var types = isStructured
            ? StructuredBindingSourceTypes
            : TextureBindingSourceTypes;
        foreach (var node in _assetGraphNodes)
        {
            foreach (var port in node.OutputPorts)
            {
                if (Array.IndexOf(types, port.Type) >= 0)
                {
                    yield return new InspectorAssetGraphRefOptionViewModel(
                        node.Id, node.DisplayName);
                    break;
                }
            }
        }
    }

    private static string? LayoutParamValue(
        EngineAssetGraphNode layoutNode,
        string paramName)
    {
        foreach (var param in layoutNode.Params)
        {
            if (string.Equals(param.Name, paramName, StringComparison.Ordinal))
            {
                return param.Value;
            }
        }
        return null;
    }

    // Enum params surface through the graph snapshot as OPTION LABELS
    // ("Structured SRV"), while a raw authored value (fixture JSON, older
    // snapshots) is the ordinal — accept both forms.
    private static bool IsStructuredBindingKind(string? value)
    {
        var text = value?.Trim();
        if (string.IsNullOrEmpty(text))
        {
            return false;
        }
        return string.Equals(
                text, "Structured SRV", StringComparison.OrdinalIgnoreCase)
            || (int.TryParse(
                    text,
                    NumberStyles.Integer,
                    CultureInfo.InvariantCulture,
                    out var ordinal)
                && ordinal == 1);
    }

    // The engine's constN_type option labels, in ordinal order (matching
    // ConstantTypeTable); the value may be either the label or the ordinal.
    private static readonly string[] ConstantTypeOptionLabels =
        ["Float", "Float2", "Float3", "Float4", "Color"];

    private static (string Label, int Width) ConstantTypeOf(string? value)
    {
        var text = value?.Trim();
        if (!string.IsNullOrEmpty(text))
        {
            if (int.TryParse(
                    text,
                    NumberStyles.Integer,
                    CultureInfo.InvariantCulture,
                    out var ordinal)
                && ordinal >= 0
                && ordinal < ConstantTypeTable.Length)
            {
                return ConstantTypeTable[ordinal];
            }
            for (var i = 0; i < ConstantTypeOptionLabels.Length; ++i)
            {
                if (string.Equals(
                        text,
                        ConstantTypeOptionLabels[i],
                        StringComparison.OrdinalIgnoreCase))
                {
                    return ConstantTypeTable[i];
                }
            }
        }
        return ConstantTypeTable[0];
    }

    // Live apply for a binding row pick/clear: upsert (or remove, id 0) the
    // semantic's row on the engine node, then mirror onto the cached tree node
    // so an immediate reselect shows the edit (house pattern).
    private void ApplyRenderableBinding(string semantic, ulong assetGraphNodeId)
    {
        if (_suppressLiveEdits || !EnsureCanApply())
        {
            return;
        }

        var response = _editorSession!.SetNodeRenderableBinding(
            NodeId, semantic, assetGraphNodeId);
        SetEditResponse(response);
        if (!response.Ok || _inspectedSceneNode is not { } node)
        {
            return;
        }

        node.RenderableBindings.RemoveAll(binding =>
            string.Equals(binding.Semantic, semantic, StringComparison.Ordinal));
        if (assetGraphNodeId != 0)
        {
            node.RenderableBindings.Add(new EngineSceneRenderableBinding
            {
                Semantic = semantic,
                AssetGraphNodeId = assetGraphNodeId,
            });
        }
    }

    // Live apply for a constant row edit/remove: a pack-time override on the
    // engine node (null value removes it) — no rebuild, next frame reflects it.
    private void ApplyRenderableParam(string name, float[]? value)
    {
        if (_suppressLiveEdits || !EnsureCanApply())
        {
            return;
        }

        var response = _editorSession!.SetNodeRenderableParam(
            NodeId, name, value);
        SetEditResponse(response);
        if (!response.Ok || _inspectedSceneNode is not { } node)
        {
            return;
        }

        node.RenderableConstants.RemoveAll(constant =>
            string.Equals(constant.Name, name, StringComparison.Ordinal));
        if (value is not null)
        {
            node.RenderableConstants.Add(new EngineSceneRenderableConstant
            {
                Name = name,
                Value = (float[])value.Clone(),
            });
        }
    }

    // Build the "GLB node" tree for the selected "Mesh from GLB scene" node: resolve
    // its connected GLB path via the asset-graph edges, import the hierarchy, and
    // expose it as a pickable tree. Defensive throughout — any gap leaves the tree
    // empty + a hint and keeps the generic `node_id` text param as the fallback.
    private void PopulateGlbNodePicker(AssetGraphNodeCardViewModel node)
    {
        HasGlbNodePicker = true;

        var glbPath = ResolveConnectedGlbPath(node);
        if (string.IsNullOrEmpty(glbPath))
        {
            GlbNodePickerHint =
                "Connect a 'Scene from GLB' node to pick a GLB node.";
            return;
        }

        if (_editorSession is null)
        {
            GlbNodePickerHint = "Engine editor session is not available.";
            return;
        }

        EngineGlbSceneHierarchy hierarchy;
        try
        {
            hierarchy = _editorSession.ImportGlbSceneHierarchy(glbPath, 0u);
        }
        catch (Exception ex)
        {
            GlbNodePickerHint = $"Couldn't read GLB: {ex.Message}";
            return;
        }

        if (hierarchy is null || !hierarchy.Ok)
        {
            GlbNodePickerHint = "Couldn't read GLB.";
            return;
        }

        var currentNodeId = CurrentNodeIdParamValue(node);
        foreach (var root in BuildGlbComponentTree(hierarchy.Components, currentNodeId))
        {
            GlbNodes.Add(root);
        }

        OnPropertyChanged(nameof(HasGlbNodes));
        if (GlbNodes.Count == 0)
        {
            GlbNodePickerHint = "The connected GLB scene has no nodes.";
        }
    }

    // Walk the asset-graph edges from the selected "Mesh from GLB scene" node to the
    // GLB file: follow the `scene` input edge to the connected "Scene from GLB"
    // node, then that node's `source_file` input edge to the file node, and read its
    // `source_path` string param (returned verbatim; the engine roots it on import).
    // Returns empty when any link is missing.
    private string ResolveConnectedGlbPath(AssetGraphNodeCardViewModel extractorNode)
    {
        // The extractor's `scene` input port index (the port carries the connected
        // Scene-from-GLB output). Fall back to a single sole input port.
        var scenePortIndex = InputPortIndexByName(
            extractorNode.InputPorts.Select(p => (p.Name, p.Index)),
            "scene");
        if (scenePortIndex is not { } sceneIndex)
        {
            return string.Empty;
        }

        var sceneNodeId = EdgeSourceInto(extractorNode.Id, sceneIndex);
        if (sceneNodeId is not { } sceneFromGlbId)
        {
            return string.Empty;
        }

        var sceneFromGlbNode = FindSnapshotNode(sceneFromGlbId);
        if (sceneFromGlbNode is null)
        {
            return string.Empty;
        }

        var sourcePortIndex = InputPortIndexByName(
            sceneFromGlbNode.InputPorts.Select(p => (p.Name, p.Index)),
            "source_file");
        if (sourcePortIndex is not { } sourceIndex)
        {
            return string.Empty;
        }

        var fileNodeId = EdgeSourceInto(sceneFromGlbId, sourceIndex);
        if (fileNodeId is not { } glbFileId)
        {
            return string.Empty;
        }

        var fileNode = FindSnapshotNode(glbFileId);
        var sourcePath = fileNode?.Params
            .FirstOrDefault(p => string.Equals(
                p.Name, "source_path", StringComparison.Ordinal))
            ?.Value;
        if (string.IsNullOrWhiteSpace(sourcePath))
        {
            return string.Empty;
        }

        // Hand the authored source_path to the engine verbatim (only whitespace
        // trimmed). The engine roots it against the project's resource root and
        // strips any "Copy as path" quote-wrapping — the editor does not
        // reimplement that path convention.
        return sourcePath!.Trim();
    }

    // The id of the node feeding `toInputPort` of `toNodeId` (the single provider an
    // input port allows), or null when nothing is connected there.
    private ulong? EdgeSourceInto(ulong toNodeId, uint toInputPort)
    {
        foreach (var edge in _assetGraphEdges)
        {
            if (edge.To == toNodeId && edge.ToInputPort == toInputPort)
            {
                return edge.From;
            }
        }

        return null;
    }

    private EngineAssetGraphNode? FindSnapshotNode(ulong id)
    {
        foreach (var node in _assetGraphNodes)
        {
            if (node.Id == id)
            {
                return node;
            }
        }

        return null;
    }

    // The index of the named input port; when no port carries that name but the node
    // has exactly one input port, use it (the link is unambiguous).
    private static uint? InputPortIndexByName(
        IEnumerable<(string Name, uint Index)> ports,
        string name)
    {
        var list = ports.ToList();
        foreach (var port in list)
        {
            if (string.Equals(port.Name, name, StringComparison.Ordinal))
            {
                return port.Index;
            }
        }

        return list.Count == 1 ? list[0].Index : null;
    }

    // The currently authored `node_id` param value (the picked GLB node), used to
    // highlight the matching tree node; empty when the param is unset/absent. Read
    // from the selected node's source params (the compiled value the engine reports),
    // since `node_id` is hidden from the generic AssetGraphParams for this node.
    private static string CurrentNodeIdParamValue(AssetGraphNodeCardViewModel node)
    {
        return node.Params
            .FirstOrDefault(p => string.Equals(
                p.Name, "node_id", StringComparison.Ordinal))
            ?.Value
            ?? string.Empty;
    }

    // Assemble the flat GLB component list into a tree by ParentId, preserving the
    // import order at each level. Each node closes over PickGlbNode so a click sets
    // the extractor's node_id; the node matching currentNodeId is marked the current
    // pick. Orphans (a ParentId not present) are surfaced as roots so nothing is lost.
    private List<InspectorGlbComponentNodeViewModel> BuildGlbComponentTree(
        IReadOnlyList<EngineGlbComponent> components,
        string currentNodeId)
    {
        var byId = new Dictionary<string, InspectorGlbComponentNodeViewModel>(
            StringComparer.Ordinal);
        var roots = new List<InspectorGlbComponentNodeViewModel>();

        // First pass: a VM per component (import order is the child order).
        var created = new List<InspectorGlbComponentNodeViewModel>(components.Count);
        foreach (var component in components)
        {
            var vm = new InspectorGlbComponentNodeViewModel(
                component,
                PickGlbNode,
                isCurrentPick: string.Equals(
                    component.Id, currentNodeId, StringComparison.Ordinal));
            created.Add(vm);
            if (!string.IsNullOrEmpty(component.Id))
            {
                byId[component.Id] = vm;
            }
        }

        // Second pass: link children to parents; un-parented (or dangling) => root.
        for (var i = 0; i < components.Count; i++)
        {
            var parentId = components[i].ParentId;
            if (!string.IsNullOrEmpty(parentId)
                && byId.TryGetValue(parentId!, out var parent)
                && !ReferenceEquals(parent, created[i]))
            {
                parent.Children.Add(created[i]);
            }
            else
            {
                roots.Add(created[i]);
            }
        }

        return roots;
    }

    // A tree node was clicked: only mesh-bearing GLB nodes are extractable, so a
    // group click is ignored (the engine extractor errors on a mesh-less node).
    // Setting node_id goes through the asset-graph param-set ABI (the tree is the
    // ONLY way to set it — node_id is hidden from the generic param editor), and the
    // current-pick highlight is moved to the clicked node.
    private void PickGlbNode(InspectorGlbComponentNodeViewModel node)
    {
        if (!node.IsSelectable)
        {
            return;
        }

        ApplyAssetGraphNodeParam("node_id", node.Id);
        if (!HasLastEditError)
        {
            HighlightCurrentGlbPick(node.Id);
        }
    }

    private void HighlightCurrentGlbPick(string nodeId)
    {
        foreach (var root in GlbNodes)
        {
            HighlightCurrentGlbPick(root, nodeId);
        }
    }

    private static void HighlightCurrentGlbPick(
        InspectorGlbComponentNodeViewModel node,
        string nodeId)
    {
        node.IsCurrentPick = string.Equals(node.Id, nodeId, StringComparison.Ordinal);
        foreach (var child in node.Children)
        {
            HighlightCurrentGlbPick(child, nodeId);
        }
    }

    private void ClearGlbNodePicker()
    {
        GlbNodes.Clear();
        HasGlbNodePicker = false;
        GlbNodePickerHint = string.Empty;
        OnPropertyChanged(nameof(HasGlbNodes));
    }

    private static bool IsMeshFromGlbSceneNode(AssetGraphNodeCardViewModel node)
    {
        return string.Equals(
            node.SchemaLabel,
            MeshFromGlbSceneSchemaLabel,
            StringComparison.Ordinal);
    }

    // The stable schema discriminator for the "Mesh from GLB scene" asset-graph node
    // (issue #213): schema_tail (low 32 bits of the SchemaID as hex) of
    // kMeshFromGLBSceneSchema 0xF11ECA55E7000414. Same field the "Scene from GLB"
    // picker matches with "e7000711".
    private const string MeshFromGlbSceneSchemaLabel = "e7000414";


    private static string FormatNullable(double? value)
    {
        return value is null ? string.Empty : FormatDouble(value.Value);
    }

    private static string FormatDouble(double value)
    {
        return value.ToString("0.###", CultureInfo.InvariantCulture);
    }

    private void NotifyComponentStateChanged()
    {
        OnPropertyChanged(nameof(HasComponents));
        OnPropertyChanged(nameof(HasNoComponents));
        OnPropertyChanged(nameof(HasBehaviors));
        OnPropertyChanged(nameof(HasNoBehaviors));
    }

    private void NotifyAssetGraphPortStateChanged()
    {
        OnPropertyChanged(nameof(HasAssetGraphInputPorts));
        OnPropertyChanged(nameof(HasNoAssetGraphInputPorts));
        OnPropertyChanged(nameof(HasAssetGraphOutputPorts));
        OnPropertyChanged(nameof(HasNoAssetGraphOutputPorts));
        OnPropertyChanged(nameof(HasAssetGraphDiagnostics));
        OnPropertyChanged(nameof(HasNoAssetGraphDiagnostics));
        OnPropertyChanged(nameof(HasAssetGraphParams));
        OnPropertyChanged(nameof(HasNoAssetGraphParams));
    }
}

public sealed class InspectorComponentViewModel
{
    public InspectorComponentViewModel(
        string name,
        string kind,
        Action<InspectorComponentViewModel> remove)
    {
        Name = name;
        Kind = kind;
        RemoveCommand = new RelayCommand(() => remove(this));
    }

    public string Name { get; }

    public string Kind { get; }

    public IRelayCommand RemoveCommand { get; }
}

// One entry in the Behaviors "+" add menu: a registered module name plus the
// command that binds it. The command lives on the item (not the inspector) so
// the MenuFlyout — which renders in a popup outside the inspector's visual
// tree — can bind to it directly.
public sealed class InspectorBehaviorModuleOptionViewModel
{
    public InspectorBehaviorModuleOptionViewModel(
        string module,
        Action<string> add)
    {
        Module = module;
        AddCommand = new RelayCommand(() => add(module));
    }

    public string Module { get; }

    public IRelayCommand AddCommand { get; }
}

// One "Scene from GLB" asset-graph node offered by the "Subtree from asset" picker
// (issue #213 piece 2): its asset-graph node id and a human label. The label falls
// back to the node id when the node has no display name so the combo is never blank.
public sealed class InspectorSceneSourceOptionViewModel
{
    public InspectorSceneSourceOptionViewModel(ulong id, string? displayName)
    {
        Id = id;
        Label = string.IsNullOrWhiteSpace(displayName)
            ? $"#{id.ToString(CultureInfo.InvariantCulture)}"
            : displayName!;
    }

    public ulong Id { get; }

    public string Label { get; }
}

// One asset-graph node offered by the "Render program" picker (issue #213).
// Carries the node id (what the render-program verb is pointed at) and a human
// label, which falls back to the node id so the combo is never blank.
// One option in the node's render-layer (draw-order) dropdown: a display label
// and the render_order value it maps to (the engine's render_layer constant).
public sealed class InspectorRenderLayerOption
{
    public InspectorRenderLayerOption(string label, int value)
    {
        Label = label;
        Value = value;
    }

    public string Label { get; }

    public int Value { get; }
}

public sealed class InspectorAssetGraphRefOptionViewModel
{
    public InspectorAssetGraphRefOptionViewModel(ulong id, string? displayName)
    {
        Id = id;
        Label = string.IsNullOrWhiteSpace(displayName)
            ? $"#{id.ToString(CultureInfo.InvariantCulture)}"
            : displayName!;
    }

    public ulong Id { get; }

    public string Label { get; }
}

// One node of the "GLB node" tree picker shown for the "Mesh from GLB scene"
// asset-graph node (issue #213). Mirrors an imported EngineGlbComponent: its GLB
// node id (what the extractor's `node_id` param is set to), display name, whether
// it carries a mesh, and its children. Only mesh-bearing nodes are pickable
// (IsSelectable) — the engine extractor errors on a mesh-less node — so group
// nodes show for structure but a click on them is ignored. IsCurrentPick tracks
// the node whose id is the node's current `node_id`, for highlighting. The Pick
// command lives on the item so the TreeView's per-node template binds to it.
public sealed class InspectorGlbComponentNodeViewModel : ViewModelBase
{
    private bool _isCurrentPick;

    public InspectorGlbComponentNodeViewModel(
        EngineGlbComponent component,
        Action<InspectorGlbComponentNodeViewModel> pick,
        bool isCurrentPick)
    {
        Id = component.Id;
        Name = string.IsNullOrWhiteSpace(component.Name) ? component.Id : component.Name;
        HasMesh = component.HasMesh;
        MeshIndex = component.MeshIndex;
        _isCurrentPick = isCurrentPick;
        PickCommand = new RelayCommand(() => pick(this));
    }

    public string Id { get; }

    public string Name { get; }

    public bool HasMesh { get; }

    public uint MeshIndex { get; }

    public ObservableCollection<InspectorGlbComponentNodeViewModel> Children { get; } = [];

    // Only mesh-bearing nodes are extractable: the engine extracts that GLB node's
    // raw mesh, and errors on a mesh-less (group) node.
    public bool IsSelectable => HasMesh;

    // Shown next to mesh-bearing nodes; group nodes are de-emphasized in the view.
    public bool IsGroup => !HasMesh;

    public bool IsCurrentPick
    {
        get => _isCurrentPick;
        set => SetProperty(ref _isCurrentPick, value);
    }

    public IRelayCommand PickCommand { get; }
}

public sealed class InspectorBehaviorViewModel : ViewModelBase
{
    private readonly Action<InspectorBehaviorViewModel> _setEnabled;
    private readonly Action<InspectorBehaviorViewModel> _applyFields;
    private readonly Action<InspectorBehaviorViewModel> _applyEvents;
    private readonly bool _initialized;
    private bool _enabled;
    private string _label;
    private string _module;
    private string _events;

    public InspectorBehaviorViewModel(
        EngineSceneBehavior behavior,
        IReadOnlyList<EngineBehaviorModuleParam> declaredParams,
        Action<InspectorBehaviorViewModel> setEnabled,
        Action<InspectorBehaviorViewModel> applyFields,
        Action<InspectorBehaviorViewModel> applyEvents,
        Action<InspectorBehaviorViewModel> remove,
        Action<string, string, string, string> writeConfig)
    {
        Id = behavior.Id;
        _enabled = behavior.Enabled;
        _label = behavior.Label;
        _module = behavior.Module;
        _events = string.Join(Environment.NewLine, behavior.Events);
        Config = BuildConfigRows(behavior, declaredParams, writeConfig);
        _setEnabled = setEnabled;
        _applyFields = applyFields;
        _applyEvents = applyEvents;
        ApplyFieldsCommand = new RelayCommand(() => _applyFields(this));
        ApplyEventsCommand = new RelayCommand(() => _applyEvents(this));
        RemoveCommand = new RelayCommand(() => remove(this));
        // Constructor assigns fields directly, so loading does not fire the live
        // enabled push; only a user toggle after construction does.
        _initialized = true;
    }

    public string Id { get; }

    public bool Enabled
    {
        get => _enabled;
        set
        {
            if (SetProperty(ref _enabled, value) && _initialized)
            {
                _setEnabled(this);
            }
        }
    }

    public string Label
    {
        get => _label;
        set => SetProperty(ref _label, value);
    }

    public string Module
    {
        get => _module;
        set => SetProperty(ref _module, value);
    }

    // Newline-delimited channel tokens (e.g. "frame.update"); the engine parses.
    public string Events
    {
        get => _events;
        set => SetProperty(ref _events, value);
    }

    public IReadOnlyList<InspectorBehaviorConfigViewModel> Config { get; }

    public bool HasConfig => Config.Count > 0;

    public bool HasNoConfig => !HasConfig;

    public IRelayCommand ApplyFieldsCommand { get; }

    public IRelayCommand ApplyEventsCommand { get; }

    public IRelayCommand RemoveCommand { get; }

    // Compiler diagnostics from the last rebuild that NAMED this module; empty when the
    // last build was clean. Shown on the card, because that is where you find out the
    // hard way that a build failed: the fields you expected simply are not there.
    private string _buildErrors = string.Empty;

    public string BuildErrors
    {
        get => _buildErrors;
        set
        {
            if (SetProperty(ref _buildErrors, value ?? string.Empty))
            {
                OnPropertyChanged(nameof(HasBuildErrors));
            }
        }
    }

    public bool HasBuildErrors => _buildErrors.Length > 0;

    // Build the editable config rows: one TYPED field per param the module DECLARES
    // (checkbox for bool, number/text box otherwise), seeded from the node's current
    // config value or the declared default; plus any non-declared config keys as text so
    // nothing is hidden. Each row writes back live via writeConfig(id, key, kind, value).
    private static List<InspectorBehaviorConfigViewModel> BuildConfigRows(
        EngineSceneBehavior behavior,
        IReadOnlyList<EngineBehaviorModuleParam> declaredParams,
        Action<string, string, string, string> writeConfig)
    {
        var rows = new List<InspectorBehaviorConfigViewModel>();
        var id = behavior.Id;
        var seen = new HashSet<string>();

        foreach (var p in declaredParams)
        {
            seen.Add(p.Key);
            var current = behavior.Config.FirstOrDefault(c => c.Name == p.Key);
            // WzBehaviorParamType: 1 = float, 2 = bool, 3 = string.
            var (renderKind, writeKind) = p.Type switch
            {
                2 => ("bool", "bool"),
                3 => ("text", "string"),
                _ => ("number", "float"),
            };

            bool boolValue = false;
            string textValue = string.Empty;
            if (renderKind == "bool")
            {
                boolValue = current is not null
                    ? current.Value is "true" or "1" or "True"
                    : p.DefaultNumber != 0.0;
            }
            else if (renderKind == "number")
            {
                textValue = current?.Value
                    ?? p.DefaultNumber.ToString("0.###", CultureInfo.InvariantCulture);
            }
            else
            {
                textValue = current?.Value ?? p.DefaultString;
            }

            var label = string.IsNullOrEmpty(p.Label) ? p.Key : p.Label;
            rows.Add(new InspectorBehaviorConfigViewModel(
                p.Key, label, renderKind, writeKind, boolValue, textValue,
                row => writeConfig(id, row.Key, row.WriteKind, row.WriteValue)));
        }

        // Config keys the module did not declare (rare) stay editable as text so the
        // editor never silently hides authored state -- EXCEPT keys a dedicated
        // inspector section owns. mind/mind_ir have the Mind picker; chart/chart_ir
        // have the Statechart Runner picker. The _ir ones are compiled blobs no one
        // hand-edits, and surfacing all four as raw text sends the author to the wrong
        // control (and a detach that blanks rather than removes a key would otherwise
        // leave a confusing empty field behind).
        foreach (var c in behavior.Config)
        {
            if (seen.Contains(c.Name) || SectionOwnedConfigKeys.Contains(c.Name))
            {
                continue;
            }
            rows.Add(new InspectorBehaviorConfigViewModel(
                c.Name, c.Name, "text",
                string.IsNullOrEmpty(c.Kind) ? "string" : c.Kind,
                false, c.Value,
                row => writeConfig(id, row.Key, row.WriteKind, row.WriteValue)));
        }

        return rows;
    }

    // Config keys managed by a purpose-built inspector section, so BuildConfigRows does
    // not also surface them as raw text fields. Keyed by name because the value is the
    // same across every module that uses it.
    private static readonly HashSet<string> SectionOwnedConfigKeys = new(StringComparer.Ordinal)
    {
        QuantumAgentMindAttachment.ConfigMind,
        QuantumAgentMindAttachment.ConfigMindIr,
        StatechartRunnerAttachment.ConfigChart,
        StatechartRunnerAttachment.ConfigChartIr,
    };
}

// One editable behavior-config field, typed by the module's declared param (checkbox
// for bool, text box for number/string). Edits push straight through the apply callback
// (SetNodeBehaviorConfig) -- no Apply button, matching the component live-edit UX.
public sealed class InspectorBehaviorConfigViewModel : ViewModelBase
{
    private readonly Action<InspectorBehaviorConfigViewModel>? _apply;
    private readonly bool _initialized;
    private bool _boolValue;
    private string _textValue;

    public InspectorBehaviorConfigViewModel(
        string key,
        string label,
        string renderKind,   // "bool" | "number" | "text"
        string writeKind,    // SetNodeBehaviorConfig kind token: "bool" | "float" | "string"
        bool boolValue,
        string textValue,
        Action<InspectorBehaviorConfigViewModel>? apply)
    {
        Key = key;
        Label = label;
        RenderKind = renderKind;
        WriteKind = writeKind;
        _boolValue = boolValue;
        _textValue = textValue;
        _apply = apply;
        _initialized = true;
    }

    public string Key { get; }

    public string Label { get; }

    public string RenderKind { get; }

    public string WriteKind { get; }

    public bool IsBool => RenderKind == "bool";

    // Number and string share a text box; only the write kind differs.
    public bool IsTextField => RenderKind is "number" or "text";

    public bool BoolValue
    {
        get => _boolValue;
        set
        {
            if (SetProperty(ref _boolValue, value) && _initialized)
            {
                _apply?.Invoke(this);
            }
        }
    }

    public string TextValue
    {
        get => _textValue;
        set
        {
            if (SetProperty(ref _textValue, value) && _initialized)
            {
                _apply?.Invoke(this);
            }
        }
    }

    // The value string SetNodeBehaviorConfig persists (bool -> "true"/"false").
    public string WriteValue => IsBool ? (_boolValue ? "true" : "false") : _textValue;
}

public sealed class InspectorAssetGraphParamViewModel : ViewModelBase
{
    private readonly Action<string, string> _apply;
    private readonly string _originalValue;
    private readonly bool _initialized;
    private string _value;
    private bool _boolValue;

    public InspectorAssetGraphParamViewModel(
        EngineAssetGraphParam param,
        Action<string, string> apply)
    {
        Name = param.Name;
        Kind = param.Kind;
        Options = param.Options;
        _originalValue = param.Value;
        _value = param.Value;
        _apply = apply;
        // Text-edited via a box that commits on LostFocus: string, file paths,
        // and numeric kinds (the engine converts the text to the declared
        // ParamType). The Value setter applies the committed value — no button.
        IsTextEditable = Kind is "string" or "filepath" or "int" or "float"
            or "float3" or "color";
        IsBool = string.Equals(Kind, "bool", StringComparison.Ordinal);
        IsEnum = string.Equals(Kind, "enum", StringComparison.Ordinal);
        _boolValue = string.Equals(_value, "true", StringComparison.OrdinalIgnoreCase);
        _initialized = true;
    }

    public string Name { get; }

    public string Kind { get; }

    public IReadOnlyList<string> Options { get; }

    public bool IsTextEditable { get; }

    public bool IsBool { get; }

    public bool IsEnum { get; }

    // Plain, non-editable display (unrecognized kinds only).
    public bool IsReadOnlyText => !IsTextEditable && !IsBool && !IsEnum;

    public string Value
    {
        get => _value;
        set
        {
            if (!SetProperty(ref _value, value))
            {
                return;
            }

            OnPropertyChanged(nameof(IsModified));
            // Apply once editing finishes — no separate Apply button (#218
            // Phase 3). Enum/color dropdowns commit on selection; text/number
            // fields commit on LostFocus (the TextBox uses UpdateSourceTrigger=
            // LostFocus), so this setter fires once per finished edit, not per
            // keystroke. Guarded by _initialized so populating the field on
            // (re)selection does not echo a spurious apply back to the engine.
            if (_initialized && (IsTextEditable || IsEnum) && value is not null)
            {
                _apply(Name, value);
            }
        }
    }

    // Bound by the checkbox for bool params; applies immediately on toggle.
    public bool BoolValue
    {
        get => _boolValue;
        set
        {
            if (!SetProperty(ref _boolValue, value))
            {
                return;
            }

            if (_initialized && IsBool)
            {
                _value = value ? "true" : "false";
                _apply(Name, _value);
            }
        }
    }

    public bool IsModified =>
        IsTextEditable
        && !string.Equals(_value, _originalValue, StringComparison.Ordinal);

    public string Detail => Kind;
}

public sealed class InspectorAssetGraphPortViewModel
{
    public InspectorAssetGraphPortViewModel(AssetGraphPortViewModel port)
    {
        Index = port.Index.ToString(CultureInfo.InvariantCulture);
        Name = port.Name;
        Label = port.Label;
        Type = port.Type.ToString(CultureInfo.InvariantCulture);
        TypeName = port.TypeName;
        Requirement = port.Flags.HasFlag(EngineAssetGraphPortFlags.Required)
            ? "required"
            : "optional";
        Arity = port.Flags.HasFlag(EngineAssetGraphPortFlags.Many)
            ? "many"
            : "single";
    }

    public string Index { get; }

    public string Name { get; }

    public string Label { get; }

    public string Type { get; }

    public string TypeName { get; }

    public string Requirement { get; }

    public string Arity { get; }

    public string DisplayName =>
        string.IsNullOrWhiteSpace(Name) ? Label : Name;

    public string Detail =>
        $"#{Index} | type {Type}"
        + (string.IsNullOrWhiteSpace(TypeName) ? string.Empty : $" | {TypeName}")
        + $" | {Requirement} | {Arity}";
}

public sealed class InspectorAssetGraphDiagnosticViewModel
{
    private const ulong InvalidId = ulong.MaxValue;

    public InspectorAssetGraphDiagnosticViewModel(
        EngineAssetGraphDiagnostic diagnostic)
    {
        Severity = diagnostic.SeverityName;
        Code = diagnostic.CodeName;
        Node = diagnostic.Node == InvalidId
            ? string.Empty
            : diagnostic.Node.ToString(CultureInfo.InvariantCulture);
        Edge = diagnostic.Edge == InvalidId
            ? string.Empty
            : diagnostic.Edge.ToString(CultureInfo.InvariantCulture);
        InputPort = diagnostic.InputPort.ToString(CultureInfo.InvariantCulture);
        Message = diagnostic.Message;
    }

    public string Severity { get; }

    public string Code { get; }

    public string Node { get; }

    public string Edge { get; }

    public string InputPort { get; }

    public string Message { get; }

    public bool HasNode => !string.IsNullOrWhiteSpace(Node);

    public bool HasEdge => !string.IsNullOrWhiteSpace(Edge);

    public string Detail
    {
        get
        {
            var parts = new List<string>
            {
                Severity,
                Code,
                $"input {InputPort}",
            };
            if (HasNode)
            {
                parts.Add($"node {Node}");
            }
            if (HasEdge)
            {
                parts.Add($"edge {Edge}");
            }
            return string.Join(" | ", parts);
        }
    }
}

public enum InspectorSelectionKind
{
    None,
    SceneNode,
    AssetGraphNode,
    SubGraph,
    StatechartDataflowNode,
    StatechartState,
}
