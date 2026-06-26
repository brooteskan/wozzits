using System;
using System.Collections.ObjectModel;
using System.Globalization;
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

    // Registered behavior modules offered by the "+" add menu, refreshed from the
    // running engine each time a scene node is inspected. Each option carries its
    // own add command so the flyout binds without reaching out of the popup.
    public ObservableCollection<InspectorBehaviorModuleOptionViewModel>
        AvailableBehaviorModules { get; } = [];

    public ObservableCollection<InspectorAssetGraphPortViewModel> AssetGraphInputPorts { get; } = [];

    public ObservableCollection<InspectorAssetGraphPortViewModel> AssetGraphOutputPorts { get; } = [];

    public ObservableCollection<InspectorAssetGraphDiagnosticViewModel> AssetGraphDiagnostics { get; } = [];

    public ObservableCollection<InspectorAssetGraphParamViewModel> AssetGraphParams { get; } = [];

    public IRelayCommand ApplyCameraCommand { get; }

    public IRelayCommand AddBehaviorCommand { get; }

    public IRelayCommand<string> AddComponentCommand { get; }

    public IRelayCommand RemoveCameraCommand { get; }

    public IRelayCommand ApplyRenderableCommand { get; }

    public IRelayCommand RemoveRenderableCommand { get; }

    // "Subtree from asset" (issue #213 piece 2).
    public IRelayCommand RemoveSubtreeComponentCommand { get; }

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

            // The subtree-from-asset reference + revealed section are session-local
            // (issue #213 piece 2), so reset them when switching nodes — each node
            // re-attaches the section via "Add Component → subtree_from_asset".
            ResetSubtreeReferenceState();

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

        foreach (var param in node.Params)
        {
            AssetGraphParams.Add(new InspectorAssetGraphParamViewModel(
                param,
                ApplyAssetGraphNodeParam));
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
            ResetSubtreeReferenceState();
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
            // Camera is shown + removed via its own parameter section below.
            if (string.Equals(component.Kind, "camera", StringComparison.Ordinal))
            {
                continue;
            }
            Components.Add(new InspectorComponentViewModel(
                component.DisplayName,
                component.Kind,
                RemoveComponent));
        }

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

        var response = _editorSession!.AddNodeComponent(NodeId, kind);
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }

        // Reflect immediately (the snapshot reconciles on its next refresh).
        // Camera has its own parameter section; the rest list as removable rows.
        if (string.Equals(kind, "camera", StringComparison.Ordinal))
        {
            HasCameraComponent = true;
        }
        else if (!Components.Any(
            c => string.Equals(c.Kind, kind, StringComparison.Ordinal)))
        {
            Components.Add(new InspectorComponentViewModel(
                ComponentDisplayName(kind), kind, RemoveComponent));
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
        // Removing the camera also hides its parameter section.
        if (string.Equals(component.Kind, "camera", StringComparison.Ordinal))
        {
            HasCameraComponent = false;
        }
        NotifyComponentStateChanged();
    }

    private static string ComponentDisplayName(string kind)
    {
        return kind switch
        {
            "camera" => "Camera",
            "renderable" => "Renderable",
            "subtree_from_asset" => "Subtree from asset",
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

        SetEditResponse(_editorSession!.SetNodeRenderableAsset(NodeId, id));
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

        SelectedSceneSourceOption = null;
        SubtreeReferenceLabel = string.Empty;
        HasSubtreeSection = false;
    }

    // Clear the picker selection + optimistic reference label and re-hide the
    // section (the reference is session-local in piece 2; switching nodes must not
    // carry it — or the revealed section — across). The section is re-attached via
    // "Add Component → subtree_from_asset" on the newly inspected node.
    private void ResetSubtreeReferenceState()
    {
        SelectedSceneSourceOption = null;
        SubtreeReferenceLabel = string.Empty;
        HasSubtreeSection = false;
    }


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
