namespace Wozzits.Editor.ViewModels.EditorPanes;

using System.Collections.ObjectModel;
using Wozzits.Editor.Protocol;

public sealed class AssetGraphNodeCardViewModel : ViewModelBase
{
    private double _x;
    private double _y;
    private bool _isSelected;
    private bool _isCanvasVisible = true;

    public AssetGraphNodeCardViewModel(EngineAssetGraphNode node, double canvasPadding)
    {
        Id = node.Id;
        Type = node.Type;
        GraphX = node.X;
        GraphY = node.Y;
        X = node.X + canvasPadding;
        Y = node.Y + canvasPadding;
        DisplayName = node.DisplayName;
        TypeName = node.TypeName;
        SchemaDisplay = node.Schema;
        SchemaLabel = node.Schema;
        CompileStatus = node.CompileStatus;
        Diagnostics = node.Diagnostics;
        Params = node.Params;
        InputPorts = new ObservableCollection<AssetGraphPortViewModel>(
            node.InputPorts.Select(port => new AssetGraphPortViewModel(this, port, isInput: true)));
        OutputPorts = new ObservableCollection<AssetGraphPortViewModel>(
            node.OutputPorts.Select(port => new AssetGraphPortViewModel(this, port, isInput: false)));
        RerouteBadges.CollectionChanged += (_, _) => OnPropertyChanged(nameof(HasReroutes));
    }

    public ulong Id { get; }

    public int Type { get; }

    public double GraphX { get; }

    public double GraphY { get; }

    public double X
    {
        get => _x;
        set => SetProperty(ref _x, value);
    }

    public double Y
    {
        get => _y;
        set => SetProperty(ref _y, value);
    }

    public bool IsSelected
    {
        get => _isSelected;
        set => SetProperty(ref _isSelected, value);
    }

    // False when this node is collapsed into a sub-graph and represented by that group's
    // proxy on the parent canvas, so its card is hidden (issue woguls/wozzits-editor#1).
    public bool IsCanvasVisible
    {
        get => _isCanvasVisible;
        set => SetProperty(ref _isCanvasVisible, value);
    }

    public string DisplayName { get; }

    public string TypeName { get; }

    public string SchemaDisplay { get; }

    // The engine's stable schema discriminator (schema_label = schema_tail: the
    // SchemaID's low 32 bits as hex). Same source as SchemaDisplay, exposed under a
    // discriminator-oriented name for schema-based matching (issue #213 piece 2).
    public string SchemaLabel { get; }

    public string CompileStatus { get; }

    public bool IsCompileError =>
        string.Equals(CompileStatus, "error", System.StringComparison.Ordinal);

    public bool IsCompileChanged =>
        string.Equals(CompileStatus, "changed", System.StringComparison.Ordinal);

    public bool IsCompileReady =>
        string.Equals(CompileStatus, "ready", System.StringComparison.Ordinal);

    public IReadOnlyList<EngineAssetGraphDiagnostic> Diagnostics { get; }

    public IReadOnlyList<EngineAssetGraphParam> Params { get; }

    public bool HasInputPorts => InputPorts.Count > 0;

    public ObservableCollection<AssetGraphPortViewModel> InputPorts { get; }

    public ObservableCollection<AssetGraphPortViewModel> OutputPorts { get; }

    // Full-width named-reroute badges shown at the bottom of the card (the port rows stay
    // fixed-width so wires still line up). Populated by the pane's projection: a "→ name"
    // declaration when this node is a reroute source, a "← name" usage per fed input
    // (issue woguls/wozzits-editor#1).
    public ObservableCollection<string> RerouteBadges { get; } = [];

    public bool HasReroutes => RerouteBadges.Count > 0;
}
