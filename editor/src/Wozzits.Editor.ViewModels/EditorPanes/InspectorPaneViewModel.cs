using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.IO;
using System.Linq;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.ViewModels.EditorPanes;

public sealed class InspectorPaneViewModel : ViewModelBase
{
    private readonly IWozzitsEngineEditorSession? _editorSession;
    private readonly Action<string>? _log;
    // Project root the GLB import roots its path against (glb_scene_source.path is
    // resource-relative, and the editor launches with the project dir as the
    // resource root — issue #213 Phase 3a). Empty in design-time/test contexts.
    private readonly string _projectDirectory;
    private string _emptyState = "No scene or asset graph node selected.";
    private string _header = string.Empty;
    private InspectorSelectionKind _selectionKind;
    private string _nodeId = string.Empty;
    private string _nodeName = string.Empty;
    private string _parentId = string.Empty;
    private bool _nodeVisible;
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
    // "Motion" section (terrain-stick track): the terrain-constraint fields of the
    // node's Motion component. Optimistic/session-local display, same as collision.
    private bool _hasMotionComponent;
    private bool _motionTerrainConstrained;
    private string _motionRideHeight = string.Empty;
    private string _motionFootprintRadius = string.Empty;
    private bool _motionAlignToSurface;
    private string _motionAlignmentStrength = string.Empty;
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

    public InspectorPaneViewModel(
        IWozzitsEngineEditorSession? editorSession = null,
        Action<string>? log = null,
        string? projectDirectory = null)
    {
        _editorSession = editorSession;
        _log = log;
        _projectDirectory = projectDirectory ?? string.Empty;
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
        // "Collision" / "Motion" (terrain-stick track): the header ✕ removes the
        // component via the generic remove verb and hides the section, like camera.
        RemoveCollisionComponentCommand = new RelayCommand(RemoveCollisionComponent);
        RemoveMotionComponentCommand = new RelayCommand(RemoveMotionComponent);
    }

    // Raised after a scene-source reference/descriptor was set or cleared on the
    // selected node (issue #213): the runtime's grafted children changed, so the
    // host (MainWindowViewModel) re-merges them into the scene tree.
    public event Action? SceneSourceChanged;

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

    // The Collision asset-graph nodes the "Collision" picker offers (terrain-stick
    // track). Threaded in from MainWindowViewModel on every selection, filtered to
    // Collision outputs (kAssetTypeCollisionAsset = 150).
    public ObservableCollection<InspectorAssetGraphRefOptionViewModel>
        AvailableCollisionSources { get; } = [];

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

    // "Collision" / "Motion" (terrain-stick track).
    public IRelayCommand RemoveCollisionComponentCommand { get; }

    public IRelayCommand RemoveMotionComponentCommand { get; }

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

            ComponentsHeader = $"{Header} Components";
            SetTransformFields(node.Transform);
            SetComponentFields(node);
        }
        finally
        {
            _suppressLiveEdits = false;
        }

        RefreshAvailableBehaviorModules();
        NotifyComponentStateChanged();
        NotifyAssetGraphPortStateChanged();
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

        SetEditResponse(_editorSession.SetAssetGraphNodeParamString(
            _assetGraphNodeIdValue,
            name,
            value));
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
            HasRenderableReference = false;
            RenderableSource = string.Empty;
            RenderableSourceKind = string.Empty;
            RenderableAssetGraphNodeId = string.Empty;
            SelectedSceneSourceOption = null;
            SubtreeReferenceLabel = string.Empty;
            HasSubtreeSection = false;
            ResetRenderProgramState();
            ResetCollisionState();
            ResetMotionState();
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
            // Camera, Collision, and Motion are shown + removed via their own
            // parameter sections below, not as generic rows.
            if (string.Equals(component.Kind, "camera", StringComparison.Ordinal)
                || string.Equals(component.Kind, "collision", StringComparison.Ordinal)
                || string.Equals(component.Kind, "motion", StringComparison.Ordinal))
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
            Behaviors.Add(CreateBehaviorViewModel(behavior));
        }
    }

    private InspectorBehaviorViewModel CreateBehaviorViewModel(
        EngineSceneBehavior behavior)
    {
        return new InspectorBehaviorViewModel(
            behavior,
            SetBehaviorEnabled,
            ApplyBehaviorFields,
            ApplyBehaviorEvents,
            RemoveBehavior);
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
            if (_inspectedSceneNode is { Camera: null } cameraNode)
            {
                cameraNode.Camera = new EngineSceneCamera();
            }
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
    // GLB file on disk: follow the `scene` input edge to the connected "Scene from
    // GLB" node, then that node's `source_file` input edge to the file node, and read
    // its `source_path` string param, resolved to an absolute path against the
    // project directory. Returns empty when any link is missing.
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

        return ResolveProjectRelativePath(sourcePath!);
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

    // Resolve a resource-relative GLB path (as authored on the file node's
    // source_path param) to an absolute filesystem path against the project
    // directory, the resource root the editor launches with (matching how the
    // GLB scene-source descriptor paths are rooted). An already-absolute path is
    // returned unchanged; with no project directory the path is returned as-is.
    private string ResolveProjectRelativePath(string sourcePath)
    {
        var trimmed = StripSurroundingQuotes(sourcePath.Trim());
        if (string.IsNullOrEmpty(trimmed))
        {
            return string.Empty;
        }

        if (Path.IsPathRooted(trimmed))
        {
            return trimmed;
        }

        if (string.IsNullOrEmpty(_projectDirectory))
        {
            return trimmed;
        }

        return Path.GetFullPath(Path.Combine(_projectDirectory, trimmed));
    }

    // Strip a single matched surrounding pair of ASCII double-quotes (belt-and-
    // suspenders with the engine's FileCarrierAssetModule::resolve_path): Windows
    // Explorer's "Copy as path" wraps the path in double-quotes ("C:\...\tank1.glb"),
    // so a source_path authored that way still imports. Only a genuine leading+
    // trailing pair is removed; interior quotes and a lone unbalanced quote stay.
    private static string StripSurroundingQuotes(string value)
    {
        if (value.Length >= 2 && value[0] == '"' && value[^1] == '"')
        {
            return value[1..^1];
        }

        return value;
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
        Action<InspectorBehaviorViewModel> setEnabled,
        Action<InspectorBehaviorViewModel> applyFields,
        Action<InspectorBehaviorViewModel> applyEvents,
        Action<InspectorBehaviorViewModel> remove)
    {
        Id = behavior.Id;
        _enabled = behavior.Enabled;
        _label = behavior.Label;
        _module = behavior.Module;
        _events = string.Join(Environment.NewLine, behavior.Events);
        Config = behavior.Config
            .Select(c => new InspectorBehaviorConfigViewModel(c.Name, c.Kind, c.Value))
            .ToList();
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
}

public sealed record InspectorBehaviorConfigViewModel(
    string Name,
    string Kind,
    string Value)
{
    public string Detail => string.IsNullOrEmpty(Kind) ? Value : $"{Kind}: {Value}";
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
        // Text-edited via a box + Apply: string, file paths, and numeric kinds
        // (the engine converts the text to the declared ParamType).
        IsTextEditable = Kind is "string" or "filepath" or "int" or "float"
            or "float3" or "color";
        IsBool = string.Equals(Kind, "bool", StringComparison.Ordinal);
        IsEnum = string.Equals(Kind, "enum", StringComparison.Ordinal);
        _boolValue = string.Equals(_value, "true", StringComparison.OrdinalIgnoreCase);
        ApplyCommand = new RelayCommand(ApplyText, () => IsTextEditable);
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
            // Enums apply immediately on a dropdown selection.
            if (_initialized && IsEnum && !string.IsNullOrEmpty(value))
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

    public IRelayCommand ApplyCommand { get; }

    public string Detail => Kind;

    private void ApplyText()
    {
        if (IsTextEditable)
        {
            _apply(Name, _value);
        }
    }
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
}
