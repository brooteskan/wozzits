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
    private bool _hasSceneSource;
    private string _sceneSourcePath = string.Empty;
    private string _sceneSourceConsumeMode = string.Empty;
    private uint _sceneSourceSceneIndex;
    private string _sceneSourceHierarchyError = string.Empty;
    // The selected GLB component in the Scene Source tree (Phase 3b-2), or null
    // when the implicit "all/base" scope is targeted. Drives the style editor.
    private GlbComponentNodeViewModel? _selectedComponent;
    // The style-editor fields (the high-impact subset): surface/wireframe on +
    // RGBA (0..1). Pre-filled from the read-back (the selected component's override
    // if present, else the base style).
    private bool _styleSurfaceEnabled;
    private string _styleSurfaceR = "0";
    private string _styleSurfaceG = "0";
    private string _styleSurfaceB = "0";
    private string _styleSurfaceA = "1";
    private bool _styleWireframeEnabled;
    private string _styleWireframeR = "0";
    private string _styleWireframeG = "0";
    private string _styleWireframeB = "0";
    private string _styleWireframeA = "1";
    // The current node's read-back descriptor (base + overrides), kept so the style
    // editor pre-fills and the override markers/optimistic updates work without a
    // snapshot reload.
    private EngineSceneNodeSceneSource? _sceneSourceModel;
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
        ClearSceneSourceCommand = new RelayCommand(ClearSceneSource);
        AssignStyleToComponentCommand =
            new RelayCommand(AssignStyleToComponent, () => HasSelectedComponent);
        AssignStyleToSubtreeCommand =
            new RelayCommand(AssignStyleToSubtree, () => HasSelectedComponent);
        AssignStyleToBaseCommand = new RelayCommand(AssignStyleToBase);
        ClearComponentStyleCommand =
            new RelayCommand(ClearComponentStyle, () => HasSelectedComponentOverride);
    }

    public ObservableCollection<InspectorComponentViewModel> Components { get; } = [];

    public ObservableCollection<InspectorBehaviorViewModel> Behaviors { get; } = [];

    // Read-only tree of the GLB scene-source's component hierarchy (issue #213
    // Phase 3b-1), imported on demand for the selected node's descriptor. The
    // grafted children are runtime-only and not in the snapshot, so this is fetched
    // separately and shown under the GLB path in the "Scene Source" card.
    public ObservableCollection<GlbComponentNodeViewModel> SceneSourceComponents
    { get; } = [];

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

    public IRelayCommand ClearSceneSourceCommand { get; }

    // Per-component style editor (issue #213 Phase 3b-2).
    public IRelayCommand AssignStyleToComponentCommand { get; }

    public IRelayCommand AssignStyleToSubtreeCommand { get; }

    public IRelayCommand AssignStyleToBaseCommand { get; }

    public IRelayCommand ClearComponentStyleCommand { get; }

    // The selected GLB component the style editor targets (null = the "all/base"
    // scope). Bound to the TreeView's SelectedItem; selecting pre-fills the editor.
    public GlbComponentNodeViewModel? SelectedComponent
    {
        get => _selectedComponent;
        set
        {
            if (SetProperty(ref _selectedComponent, value))
            {
                OnSelectedComponentChanged();
            }
        }
    }

    public bool HasSelectedComponent => _selectedComponent is not null;

    // True when the selected component carries a per-mesh override (enables the
    // "Clear override" action). False for "all/base" or an unstyled component.
    public bool HasSelectedComponentOverride =>
        _selectedComponent is { HasMesh: true, HasOverride: true };

    // Label describing the current style-editor target, e.g. "turret (mesh 2)" or
    // "All components (base)".
    public string StyleTargetLabel
    {
        get
        {
            if (_selectedComponent is null)
            {
                return "All components (base)";
            }
            return _selectedComponent.HasMesh
                ? $"{_selectedComponent.Name} (mesh {_selectedComponent.MeshIndex})"
                : $"{_selectedComponent.Name} (no mesh)";
        }
    }

    // True when the style editor can author a per-component/subtree assignment:
    // there is a descriptor and a selected component that (for the single-component
    // assign) carries a mesh. Subtree assign is allowed on any selected component
    // (it fans out to descendant meshes).
    public bool CanAssignToComponent =>
        HasSceneSource && _selectedComponent is { HasMesh: true };

    public bool StyleSurfaceEnabled
    {
        get => _styleSurfaceEnabled;
        set => SetProperty(ref _styleSurfaceEnabled, value);
    }

    public string StyleSurfaceR
    {
        get => _styleSurfaceR;
        set => SetProperty(ref _styleSurfaceR, value);
    }

    public string StyleSurfaceG
    {
        get => _styleSurfaceG;
        set => SetProperty(ref _styleSurfaceG, value);
    }

    public string StyleSurfaceB
    {
        get => _styleSurfaceB;
        set => SetProperty(ref _styleSurfaceB, value);
    }

    public string StyleSurfaceA
    {
        get => _styleSurfaceA;
        set => SetProperty(ref _styleSurfaceA, value);
    }

    public bool StyleWireframeEnabled
    {
        get => _styleWireframeEnabled;
        set => SetProperty(ref _styleWireframeEnabled, value);
    }

    public string StyleWireframeR
    {
        get => _styleWireframeR;
        set => SetProperty(ref _styleWireframeR, value);
    }

    public string StyleWireframeG
    {
        get => _styleWireframeG;
        set => SetProperty(ref _styleWireframeG, value);
    }

    public string StyleWireframeB
    {
        get => _styleWireframeB;
        set => SetProperty(ref _styleWireframeB, value);
    }

    public string StyleWireframeA
    {
        get => _styleWireframeA;
        set => SetProperty(ref _styleWireframeA, value);
    }

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

    // True when the selected node carries a GLB scene-source descriptor (Phase 2
    // snapshot field). Drives the "Scene Source" card between its show/clear state
    // and its "Import GLB…" state (issue #213 Phase 3a).
    public bool HasSceneSource
    {
        get => _hasSceneSource;
        private set
        {
            if (SetProperty(ref _hasSceneSource, value))
            {
                OnPropertyChanged(nameof(HasNoSceneSource));
            }
        }
    }

    public bool HasNoSceneSource => !_hasSceneSource;

    public string SceneSourcePath
    {
        get => _sceneSourcePath;
        private set => SetProperty(ref _sceneSourcePath, value);
    }

    public string SceneSourceConsumeMode
    {
        get => _sceneSourceConsumeMode;
        private set => SetProperty(ref _sceneSourceConsumeMode, value);
    }

    public bool HasSceneSourceComponents => SceneSourceComponents.Count > 0;

    // Set only when the descriptor is present but its GLB could not be imported
    // (missing file, parse failure). Shown as a tiny line under the path; an empty
    // hierarchy with no error simply renders nothing.
    public string SceneSourceHierarchyError
    {
        get => _sceneSourceHierarchyError;
        private set
        {
            if (SetProperty(ref _sceneSourceHierarchyError, value))
            {
                OnPropertyChanged(nameof(HasSceneSourceHierarchyError));
            }
        }
    }

    public bool HasSceneSourceHierarchyError =>
        !string.IsNullOrWhiteSpace(SceneSourceHierarchyError);

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

            SetSceneSourceFields(node.SceneSource);

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
            SetSceneSourceFields(null);
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

    // Read-only mirror of a node's GLB scene-source descriptor (Phase 2 snapshot)
    // into the "Scene Source" card. null => no descriptor (the card shows its
    // "Import GLB…" state instead). When a descriptor is present, also refresh the
    // read-only component-hierarchy tree (Phase 3b-1).
    private void SetSceneSourceFields(EngineSceneNodeSceneSource? sceneSource)
    {
        HasSceneSource = sceneSource is not null;
        SceneSourcePath = sceneSource?.Path ?? string.Empty;
        SceneSourceConsumeMode = sceneSource?.ConsumeMode ?? string.Empty;
        _sceneSourceSceneIndex = sceneSource?.SceneIndex ?? 0u;
        _sceneSourceModel = sceneSource;
        // Reset the style editor to the base scope when the selection changes node.
        SelectedComponent = null;
        RefreshSceneSourceHierarchy();
        PrefillStyleEditor();
    }

    // Import + (re)build the read-only GLB component-hierarchy tree for the current
    // descriptor (issue #213 Phase 3b-1). Defensive by construction: no descriptor,
    // an empty path, an unavailable session, or an ok=0/throwing import all just
    // leave the tree empty (with a tiny error line only when the import reported a
    // reason). Never throws — a missing GLB must not break inspecting the node.
    private void RefreshSceneSourceHierarchy()
    {
        SceneSourceComponents.Clear();
        SceneSourceHierarchyError = string.Empty;

        if (!HasSceneSource
            || string.IsNullOrWhiteSpace(SceneSourcePath)
            || _editorSession is null)
        {
            OnPropertyChanged(nameof(HasSceneSourceComponents));
            return;
        }

        var absolutePath = ResolveSceneSourceAbsolutePath(SceneSourcePath);

        EngineGlbSceneHierarchy hierarchy;
        try
        {
            hierarchy = _editorSession.ImportGlbSceneHierarchy(
                absolutePath,
                _sceneSourceSceneIndex);
        }
        catch (Exception ex)
        {
            // The session forwards to a read-only native import that already
            // fails closed; this is belt-and-suspenders so the inspector is
            // never taken down by a bad GLB.
            SceneSourceHierarchyError = ex.Message;
            OnPropertyChanged(nameof(HasSceneSourceComponents));
            return;
        }

        if (!hierarchy.Ok)
        {
            SceneSourceHierarchyError = string.IsNullOrWhiteSpace(hierarchy.Error)
                ? "Couldn't read GLB."
                : $"Couldn't read GLB: {hierarchy.Error}";
            OnPropertyChanged(nameof(HasSceneSourceComponents));
            return;
        }

        foreach (var root in BuildGlbComponentTree(hierarchy.Components))
        {
            SceneSourceComponents.Add(root);
        }
        MarkOverriddenComponents();
        OnPropertyChanged(nameof(HasSceneSourceComponents));
    }

    // Set HasOverride on every tree component whose mesh_index is in the
    // descriptor's read-back override table (Phase 3b-2), so the UI marks styled
    // components. Cheap linear walk over the (small) tree.
    private void MarkOverriddenComponents()
    {
        var overridden = _sceneSourceModel is null
            ? new HashSet<uint>()
            : _sceneSourceModel.StyleOverrides
                .Select(o => o.MeshIndex)
                .ToHashSet();

        void Walk(GlbComponentNodeViewModel node)
        {
            node.HasOverride = node.HasMesh && overridden.Contains(node.MeshIndex);
            foreach (var child in node.Children)
            {
                Walk(child);
            }
        }

        foreach (var root in SceneSourceComponents)
        {
            Walk(root);
        }
    }

    // Assemble the flat component list into a tree via ParentId. Components whose
    // parent is missing (or null) become roots; child order follows the engine's
    // depth-first list order. Defends against a cycle/dangling parent by treating
    // any not-yet-seen parent as absent (so every component is placed exactly once).
    private static List<GlbComponentNodeViewModel> BuildGlbComponentTree(
        IReadOnlyList<EngineGlbComponent> components)
    {
        var byId = new Dictionary<string, GlbComponentNodeViewModel>(
            StringComparer.Ordinal);
        var roots = new List<GlbComponentNodeViewModel>();

        // First pass: create a node per component, keyed by id (first id wins on
        // the unlikely event of a duplicate).
        foreach (var component in components)
        {
            var node = new GlbComponentNodeViewModel(component);
            if (!string.IsNullOrEmpty(component.Id))
            {
                byId.TryAdd(component.Id, node);
            }
        }

        // Second pass: attach each node to its parent (or promote to a root).
        foreach (var component in components)
        {
            var node = !string.IsNullOrEmpty(component.Id)
                && byId.TryGetValue(component.Id, out var existing)
                    ? existing
                    : new GlbComponentNodeViewModel(component);

            if (!string.IsNullOrEmpty(component.ParentId)
                && byId.TryGetValue(component.ParentId, out var parent)
                && !ReferenceEquals(parent, node))
            {
                parent.Children.Add(node);
            }
            else
            {
                roots.Add(node);
            }
        }

        return roots;
    }

    // Resolve the resource-relative descriptor path against the project directory
    // (the resource root the editor launches with). An already-absolute path (or an
    // unknown project dir) is returned unchanged — the native import treats an
    // absolute path as-is. Mirrors ToProjectRelativeResourcePath in reverse.
    private string ResolveSceneSourceAbsolutePath(string descriptorPath)
    {
        if (System.IO.Path.IsPathRooted(descriptorPath)
            || string.IsNullOrEmpty(_projectDirectory))
        {
            return descriptorPath;
        }
        return System.IO.Path.Combine(_projectDirectory, descriptorPath);
    }

    // Author a GLB scene-source descriptor on the selected node from an absolute
    // picked file path (issue #213 Phase 3a). The View runs the file dialog and
    // hands the absolute path here; we root it against the project directory (GLB
    // descriptor paths are resource-relative, and the editor launches with the
    // project dir as the resource root) and push it live as an Instance import at
    // scene_index 0 with the single default render style (no per-component styling
    // — that is Phase 3b). On success the snapshot refresh repopulates the card;
    // we also set the local state so it reflects immediately.
    public void ImportGlbSceneSource(string absolutePath)
    {
        if (!EnsureCanApply())
        {
            return;
        }
        if (string.IsNullOrWhiteSpace(absolutePath))
        {
            return;
        }

        var resourcePath = ToProjectRelativeResourcePath(absolutePath);

        var response = _editorSession!.SetNodeGlbSceneSource(
            NodeId,
            resourcePath,
            sceneIndex: 0u,
            consumeMode: 0u);   // 0 = WZ_SCENE_SOURCE_INSTANCE
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }
        HasSceneSource = true;
        SceneSourcePath = resourcePath;
        SceneSourceConsumeMode = "instance";
        _sceneSourceSceneIndex = 0u;

        // Refresh the cached model so re-selecting the node reflects the import
        // (mirrors the live transform-edit fix), without a snapshot reload. A
        // freshly imported GLB carries no styling yet (base/overrides empty — the
        // engine applies its built-in default style).
        var imported = new EngineSceneNodeSceneSource
        {
            Kind = "glb",
            Path = resourcePath,
            ConsumeMode = "instance",
            SceneIndex = 0u,
        };
        _sceneSourceModel = imported;
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.SceneSource = imported;
        }
        SelectedComponent = null;

        // Surface the freshly imported GLB's component hierarchy right away
        // (Phase 3b-1), matching what re-selecting the node would show, and reset
        // the style editor to the base scope.
        RefreshSceneSourceHierarchy();
        PrefillStyleEditor();
    }

    private void ClearSceneSource()
    {
        if (!EnsureCanApply())
        {
            return;
        }

        // An empty path clears the descriptor on the engine side.
        var response = _editorSession!.SetNodeGlbSceneSource(
            NodeId, string.Empty, sceneIndex: 0u, consumeMode: 0u);
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }
        SetSceneSourceFields(null);
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.SceneSource = null;
        }
    }

    // ─── Per-component style editor (issue #213 Phase 3b-2) ───────────────────

    // When the tree selection changes, refresh the editor's enable-state and
    // pre-fill the fields from the new target's existing style (the selected
    // component's override if present, else the base).
    private void OnSelectedComponentChanged()
    {
        OnPropertyChanged(nameof(HasSelectedComponent));
        OnPropertyChanged(nameof(HasSelectedComponentOverride));
        OnPropertyChanged(nameof(CanAssignToComponent));
        OnPropertyChanged(nameof(StyleTargetLabel));
        AssignStyleToComponentCommand.NotifyCanExecuteChanged();
        AssignStyleToSubtreeCommand.NotifyCanExecuteChanged();
        ClearComponentStyleCommand.NotifyCanExecuteChanged();
        PrefillStyleEditor();
    }

    // Pre-fill the style-editor fields from the read-back: the selected
    // component's override if it has one, else the descriptor's base style, else
    // engine-ish defaults. Suppress-free: these are plain field writes (no live
    // echo — the style is only pushed on an explicit Assign).
    private void PrefillStyleEditor()
    {
        EngineGlbStyle? style = null;
        if (_sceneSourceModel is not null)
        {
            if (_selectedComponent is { HasMesh: true } sel)
            {
                style = _sceneSourceModel.StyleOverrides
                    .FirstOrDefault(o => o.MeshIndex == sel.MeshIndex)?.Style;
            }
            style ??= _sceneSourceModel.HasBaseStyle
                ? _sceneSourceModel.BaseStyle
                : null;
        }

        StyleSurfaceEnabled = style?.SurfaceEnabled ?? false;
        StyleWireframeEnabled = style?.WireframeEnabled ?? true;
        SetRgbaFields(
            style?.SurfaceRgba,
            v => StyleSurfaceR = v,
            v => StyleSurfaceG = v,
            v => StyleSurfaceB = v,
            v => StyleSurfaceA = v,
            fallbackAlpha: "1");
        SetRgbaFields(
            style?.WireframeRgba,
            v => StyleWireframeR = v,
            v => StyleWireframeG = v,
            v => StyleWireframeB = v,
            v => StyleWireframeA = v,
            fallbackAlpha: "1");
    }

    private static void SetRgbaFields(
        float[]? rgba,
        Action<string> setR,
        Action<string> setG,
        Action<string> setB,
        Action<string> setA,
        string fallbackAlpha)
    {
        setR(FormatChannel(rgba, 0, "0"));
        setG(FormatChannel(rgba, 1, "0"));
        setB(FormatChannel(rgba, 2, "0"));
        setA(FormatChannel(rgba, 3, fallbackAlpha));
    }

    private static string FormatChannel(float[]? rgba, int index, string fallback)
    {
        if (rgba is null || index >= rgba.Length)
        {
            return fallback;
        }
        return rgba[index].ToString("0.###", CultureInfo.InvariantCulture);
    }

    // Assign the editor's style as a per-mesh override for the selected component
    // (Phase 3b-2). No-op (with a hint) if the selected component has no mesh.
    private void AssignStyleToComponent()
    {
        if (!EnsureCanApply())
        {
            return;
        }
        if (_selectedComponent is not { HasMesh: true } component)
        {
            LastEditError =
                "Select a component with a mesh to assign a per-component style.";
            return;
        }

        var (surfaceEnabled, surfaceRgba, wireframeEnabled, wireframeRgba) =
            CurrentEditorStyle();
        var response = _editorSession!.SetNodeGlbComponentStyle(
            NodeId,
            targetBase: false,
            meshIndex: component.MeshIndex,
            surfaceEnabled,
            surfaceRgba,
            wireframeEnabled,
            wireframeRgba);
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }

        ApplyOverrideOptimistically(
            component.MeshIndex,
            surfaceEnabled,
            surfaceRgba,
            wireframeEnabled,
            wireframeRgba);
    }

    // Assign the editor's style as a per-mesh override for EVERY mesh under the
    // selected component (the subtree fan-out, Phase 3b-2): collect descendant
    // mesh indices from the hierarchy and set an override for each.
    private void AssignStyleToSubtree()
    {
        if (!EnsureCanApply())
        {
            return;
        }
        if (_selectedComponent is null)
        {
            LastEditError = "Select a component to assign a subtree style.";
            return;
        }

        var meshIndices = _selectedComponent.CollectSubtreeMeshIndices()
            .Distinct()
            .ToList();
        if (meshIndices.Count == 0)
        {
            LastEditError = "No meshes under the selected component.";
            return;
        }

        var (surfaceEnabled, surfaceRgba, wireframeEnabled, wireframeRgba) =
            CurrentEditorStyle();
        foreach (var meshIndex in meshIndices)
        {
            var response = _editorSession!.SetNodeGlbComponentStyle(
                NodeId,
                targetBase: false,
                meshIndex,
                surfaceEnabled,
                surfaceRgba,
                wireframeEnabled,
                wireframeRgba);
            if (!response.Ok)
            {
                SetEditResponse(response);
                return;
            }
            ApplyOverrideOptimistically(
                meshIndex,
                surfaceEnabled,
                surfaceRgba,
                wireframeEnabled,
                wireframeRgba);
        }
        LastEditError = string.Empty;
    }

    // Assign the editor's style as the descriptor's BASE style (applies to every
    // imported mesh that has no override). The "all / base" scope (Phase 3b-2).
    private void AssignStyleToBase()
    {
        if (!EnsureCanApply())
        {
            return;
        }
        if (!HasSceneSource)
        {
            LastEditError = "This node has no GLB scene source.";
            return;
        }

        var (surfaceEnabled, surfaceRgba, wireframeEnabled, wireframeRgba) =
            CurrentEditorStyle();
        var response = _editorSession!.SetNodeGlbComponentStyle(
            NodeId,
            targetBase: true,
            meshIndex: 0u,
            surfaceEnabled,
            surfaceRgba,
            wireframeEnabled,
            wireframeRgba);
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }

        ApplyBaseStyleOptimistically(
            surfaceEnabled, surfaceRgba, wireframeEnabled, wireframeRgba);
    }

    // Clear the selected component's per-mesh override (it falls back to base).
    private void ClearComponentStyle()
    {
        if (!EnsureCanApply())
        {
            return;
        }
        if (_selectedComponent is not { HasMesh: true } component)
        {
            return;
        }

        var response = _editorSession!.ClearNodeGlbComponentStyle(
            NodeId, component.MeshIndex);
        SetEditResponse(response);
        if (!response.Ok)
        {
            return;
        }

        RemoveOverrideOptimistically(component.MeshIndex);
    }

    // Build the engine-facing style from the editor fields: the two enable flags +
    // RGBA float[4] arrays parsed from the channel text (out-of-range/garbage
    // clamps to a sane default so a typo never sends NaN).
    private (bool, float[], bool, float[]) CurrentEditorStyle()
    {
        var surfaceRgba = new[]
        {
            ParseChannel(StyleSurfaceR),
            ParseChannel(StyleSurfaceG),
            ParseChannel(StyleSurfaceB),
            ParseChannel(StyleSurfaceA),
        };
        var wireframeRgba = new[]
        {
            ParseChannel(StyleWireframeR),
            ParseChannel(StyleWireframeG),
            ParseChannel(StyleWireframeB),
            ParseChannel(StyleWireframeA),
        };
        return (StyleSurfaceEnabled, surfaceRgba, StyleWireframeEnabled, wireframeRgba);
    }

    private static float ParseChannel(string text)
    {
        if (float.TryParse(
                text,
                NumberStyles.Float,
                CultureInfo.InvariantCulture,
                out var value))
        {
            return Math.Clamp(value, 0f, 1f);
        }
        return 0f;
    }

    private static EngineGlbStyle BuildEngineStyle(
        bool surfaceEnabled,
        float[] surfaceRgba,
        bool wireframeEnabled,
        float[] wireframeRgba)
    {
        return new EngineGlbStyle
        {
            SurfaceEnabled = surfaceEnabled,
            SurfaceRgba = (float[])surfaceRgba.Clone(),
            WireframeEnabled = wireframeEnabled,
            WireframeRgba = (float[])wireframeRgba.Clone(),
        };
    }

    // Optimistic-update the cached descriptor model's override table + the tree
    // marker so the assignment shows immediately (the viewport re-renders from the
    // engine re-materialize; the JSON snapshot only catches up on save/reload).
    private void ApplyOverrideOptimistically(
        uint meshIndex,
        bool surfaceEnabled,
        float[] surfaceRgba,
        bool wireframeEnabled,
        float[] wireframeRgba)
    {
        LastEditError = string.Empty;
        var style = BuildEngineStyle(
            surfaceEnabled, surfaceRgba, wireframeEnabled, wireframeRgba);
        UpdateCachedSceneSource(model =>
        {
            var overrides = model.StyleOverrides
                .Where(o => o.MeshIndex != meshIndex)
                .Append(new EngineGlbStyleOverride
                {
                    MeshIndex = meshIndex,
                    Style = style,
                })
                .ToList();
            return model with
            {
                StyleOverrides = overrides,
                StyleOverrideCount = (uint)overrides.Count,
            };
        });
        MarkOverriddenComponents();
        OnPropertyChanged(nameof(HasSelectedComponentOverride));
        ClearComponentStyleCommand.NotifyCanExecuteChanged();
    }

    private void RemoveOverrideOptimistically(uint meshIndex)
    {
        LastEditError = string.Empty;
        UpdateCachedSceneSource(model =>
        {
            var overrides = model.StyleOverrides
                .Where(o => o.MeshIndex != meshIndex)
                .ToList();
            return model with
            {
                StyleOverrides = overrides,
                StyleOverrideCount = (uint)overrides.Count,
            };
        });
        MarkOverriddenComponents();
        OnPropertyChanged(nameof(HasSelectedComponentOverride));
        ClearComponentStyleCommand.NotifyCanExecuteChanged();
        // Re-prefill so the editor now reflects the base (the override is gone).
        PrefillStyleEditor();
    }

    private void ApplyBaseStyleOptimistically(
        bool surfaceEnabled,
        float[] surfaceRgba,
        bool wireframeEnabled,
        float[] wireframeRgba)
    {
        LastEditError = string.Empty;
        var style = BuildEngineStyle(
            surfaceEnabled, surfaceRgba, wireframeEnabled, wireframeRgba);
        UpdateCachedSceneSource(model => model with
        {
            HasBaseStyle = true,
            BaseStyle = style,
        });
    }

    // Apply `mutate` to both the inspector's cached model (_sceneSourceModel) and
    // the tree node's cached SceneSource so re-selecting the node reflects the
    // edit without a snapshot reload (mirrors the live transform-edit fix).
    private void UpdateCachedSceneSource(
        Func<EngineSceneNodeSceneSource, EngineSceneNodeSceneSource> mutate)
    {
        if (_sceneSourceModel is null)
        {
            return;
        }
        _sceneSourceModel = mutate(_sceneSourceModel);
        if (_inspectedSceneNode is not null)
        {
            _inspectedSceneNode.SceneSource = _sceneSourceModel;
        }
    }

    // Root an absolute file path against the project directory using forward
    // slashes (the convention for resource-relative paths like "gltf/tank1.glb").
    // Files outside the project tree (or when the project dir is unknown) keep
    // their absolute path — the engine resolves an absolute path as-is.
    private string ToProjectRelativeResourcePath(string absolutePath)
    {
        if (string.IsNullOrEmpty(_projectDirectory))
        {
            return absolutePath;
        }

        var relative = System.IO.Path.GetRelativePath(
            _projectDirectory, absolutePath);
        if (relative.StartsWith("..", StringComparison.Ordinal)
            || System.IO.Path.IsPathRooted(relative))
        {
            return absolutePath;
        }
        return relative.Replace('\\', '/');
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

// One node in the GLB scene-source component tree (issue #213 Phase 3b-1/3b-2).
// 3b-2 makes it interactive: the TreeView selects it (IsSelected drives the style
// editor) and HasOverride marks components carrying a per-mesh style override. The
// tree is assembled from the flat list via parent_id; CollectSubtreeMeshIndices
// fans an assignment out over a whole subtree.
public sealed class GlbComponentNodeViewModel : ViewModelBase
{
    private bool _hasOverride;

    public GlbComponentNodeViewModel(EngineGlbComponent component)
    {
        Id = component.Id;
        Name = string.IsNullOrEmpty(component.Name) ? component.Id : component.Name;
        HasMesh = component.HasMesh;
        MeshIndex = component.MeshIndex;
        NodeIndex = component.NodeIndex;
    }

    public string Id { get; }

    public string Name { get; }

    public bool HasMesh { get; }

    public uint MeshIndex { get; }

    public uint NodeIndex { get; }

    // A small badge next to mesh-bearing components (empty when the component has
    // no mesh, so the marker is hidden).
    public string MeshBadge => HasMesh ? "mesh" : string.Empty;

    // True when this mesh-bearing component currently carries a per-mesh style
    // override in the descriptor (drives a small marker; Phase 3b-2). Derived from
    // the read-back override table.
    public bool HasOverride
    {
        get => _hasOverride;
        set
        {
            if (SetProperty(ref _hasOverride, value))
            {
                OnPropertyChanged(nameof(OverrideBadge));
            }
        }
    }

    public string OverrideBadge => HasOverride ? "styled" : string.Empty;

    public ObservableCollection<GlbComponentNodeViewModel> Children { get; } = [];

    // Every mesh_index at or under this component (depth-first), for fanning a
    // "Assign to subtree" out over the whole branch. Components with no mesh
    // contribute nothing themselves but their descendants still count.
    public IReadOnlyList<uint> CollectSubtreeMeshIndices()
    {
        var indices = new List<uint>();
        Collect(this, indices);
        return indices;
    }

    private static void Collect(
        GlbComponentNodeViewModel node,
        List<uint> into)
    {
        if (node.HasMesh)
        {
            into.Add(node.MeshIndex);
        }
        foreach (var child in node.Children)
        {
            Collect(child, into);
        }
    }
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
