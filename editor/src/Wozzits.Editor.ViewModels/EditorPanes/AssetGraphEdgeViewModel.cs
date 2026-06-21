namespace Wozzits.Editor.ViewModels.EditorPanes;

using System.ComponentModel;
using Wozzits.Editor.Protocol;

public sealed class AssetGraphEdgeViewModel : ViewModelBase, IDisposable
{
    private readonly AssetGraphNodeCardViewModel _from;
    private readonly AssetGraphNodeCardViewModel _to;
    private readonly double _cardWidth;
    private readonly double _portRowBaseY;
    private readonly double _portRowSpacing;
    private bool _disposed;

    public AssetGraphEdgeViewModel(
        EngineAssetGraphEdge edge,
        AssetGraphNodeCardViewModel from,
        AssetGraphNodeCardViewModel to,
        double cardWidth,
        double portRowBaseY,
        double portRowSpacing)
    {
        Id = edge.Id;
        FromNodeId = edge.From;
        ToNodeId = edge.To;
        ToInputPort = edge.ToInputPort;
        _from = from;
        _to = to;
        _cardWidth = cardWidth;
        _portRowBaseY = portRowBaseY;
        _portRowSpacing = portRowSpacing;

        _from.PropertyChanged += NodePositionChanged;
        _to.PropertyChanged += NodePositionChanged;
    }

    public ulong Id { get; }

    public ulong FromNodeId { get; }

    public ulong ToNodeId { get; }

    public uint ToInputPort { get; }

    public double StartX => _from.X + _cardWidth;

    public double StartY => _from.Y + _portRowBaseY;

    public double EndX => _to.X;

    public double EndY => _to.Y + _portRowBaseY + ToInputPort * _portRowSpacing;

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _from.PropertyChanged -= NodePositionChanged;
        _to.PropertyChanged -= NodePositionChanged;
        _disposed = true;
    }

    private void NodePositionChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is not nameof(AssetGraphNodeCardViewModel.X)
            and not nameof(AssetGraphNodeCardViewModel.Y))
        {
            return;
        }

        OnPropertyChanged(nameof(StartX));
        OnPropertyChanged(nameof(StartY));
        OnPropertyChanged(nameof(EndX));
        OnPropertyChanged(nameof(EndY));
    }
}
