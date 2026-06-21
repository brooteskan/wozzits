using System.Collections.ObjectModel;
using System.Globalization;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.ViewModels.EditorPanes;

public sealed class InspectorPaneViewModel : ViewModelBase
{
    private readonly IWozzitsEngineEditorSession? _editorSession;
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

    public InspectorPaneViewModel(
        IWozzitsEngineEditorSession? editorSession = null)
    {
        _editorSession = editorSession;
        ApplyNodePropertiesCommand = new RelayCommand(
            ApplyNodeProperties,
            () => HasSceneNodeSelection);
        ApplyTransformCommand = new RelayCommand(
            ApplyTransform,
            () => HasSceneNodeSelection && HasTransform);
        ApplyCameraCommand = new RelayCommand(
            ApplyCamera,
            () => HasSceneNodeSelection && HasCameraComponent);
    }

    public ObservableCollection<InspectorComponentViewModel> Components { get; } = [];

    public ObservableCollection<InspectorAssetGraphPortViewModel> AssetGraphInputPorts { get; } = [];

    public ObservableCollection<InspectorAssetGraphPortViewModel> AssetGraphOutputPorts { get; } = [];

    public ObservableCollection<InspectorAssetGraphDiagnosticViewModel> AssetGraphDiagnostics { get; } = [];

    public ObservableCollection<InspectorAssetGraphParamViewModel> AssetGraphParams { get; } = [];

    public IRelayCommand ApplyNodePropertiesCommand { get; }

    public IRelayCommand ApplyTransformCommand { get; }

    public IRelayCommand ApplyCameraCommand { get; }

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
        set => SetProperty(ref _nodeName, value);
    }

    public string ParentId
    {
        get => _parentId;
        private set => SetProperty(ref _parentId, value);
    }

    public bool NodeVisible
    {
        get => _nodeVisible;
        set => SetProperty(ref _nodeVisible, value);
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

    public bool HasTransform
    {
        get => _hasTransform;
        private set
        {
            if (SetProperty(ref _hasTransform, value))
            {
                ApplyTransformCommand.NotifyCanExecuteChanged();
            }
        }
    }

    public string TranslationX
    {
        get => _translationX;
        set => SetProperty(ref _translationX, value);
    }

    public string TranslationY
    {
        get => _translationY;
        set => SetProperty(ref _translationY, value);
    }

    public string TranslationZ
    {
        get => _translationZ;
        set => SetProperty(ref _translationZ, value);
    }

    public string RotationX
    {
        get => _rotationX;
        set => SetProperty(ref _rotationX, value);
    }

    public string RotationY
    {
        get => _rotationY;
        set => SetProperty(ref _rotationY, value);
    }

    public string RotationZ
    {
        get => _rotationZ;
        set => SetProperty(ref _rotationZ, value);
    }

    public string ScaleX
    {
        get => _scaleX;
        set => SetProperty(ref _scaleX, value);
    }

    public string ScaleY
    {
        get => _scaleY;
        set => SetProperty(ref _scaleY, value);
    }

    public string ScaleZ
    {
        get => _scaleZ;
        set => SetProperty(ref _scaleZ, value);
    }

    public bool HasComponents => Components.Count > 0;

    public bool HasNoComponents => !HasComponents;

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

        NodeId = node.Id;
        NodeName = node.DisplayName;
        ParentId = node.ParentId ?? string.Empty;
        NodeVisible = node.Visible ?? false;

        HasRenderableReference = node.Renderable is not null;
        RenderableSource = node.RenderableSource.DisplayName;
        RenderableSourceKind = node.RenderableSource.Kind;
        RenderableAssetGraphNodeId =
            node.Renderable?.AssetGraphNodeId?.ToString(CultureInfo.InvariantCulture) ?? string.Empty;

        ComponentsHeader = $"{Header} Components";
        SetTransformFields(node.Transform);
        SetComponentFields(node);
        NotifyComponentStateChanged();
        NotifyAssetGraphPortStateChanged();
    }

    public void Inspect(AssetGraphNodeCardViewModel? node)
    {
        Components.Clear();
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

    private void ApplyNodeProperties()
    {
        if (!EnsureCanApply())
        {
            return;
        }

        SetEditResponse(_editorSession!.SetSceneNodeProperties(
            NodeId,
            NodeName,
            NodeVisible));
    }

    private void ApplyTransform()
    {
        if (!EnsureCanApply())
        {
            return;
        }

        SetEditResponse(_editorSession!.SetSceneNodeTransform(
            NodeId,
            new EngineSceneTransformEdit
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
            }));
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
        NodeId = string.Empty;
        NodeName = string.Empty;
        ParentId = string.Empty;
        NodeVisible = false;
        HasRenderableReference = false;
        RenderableSource = string.Empty;
        RenderableSourceKind = string.Empty;
        RenderableAssetGraphNodeId = string.Empty;
        ComponentsHeader = "Components";
        SetTransformFields(null);
        HasCameraComponent = false;
        CameraFovY = string.Empty;
        CameraNear = string.Empty;
        CameraFar = string.Empty;
        CameraAspect = string.Empty;
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
        ApplyNodePropertiesCommand.NotifyCanExecuteChanged();
        ApplyTransformCommand.NotifyCanExecuteChanged();
        ApplyCameraCommand.NotifyCanExecuteChanged();
    }

    private void SetTransformFields(EngineSceneTransform? transform)
    {
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
            Components.Add(new InspectorComponentViewModel(
                component.DisplayName,
                component.Kind));
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

public sealed record InspectorComponentViewModel(string Name, string Kind);

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
