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
    private AssetGraphSubGraph? _fromProxy;
    private AssetGraphSubGraph? _toProxy;
    private bool _isRenderHidden;
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

    // When a sub-graph collapses this edge's source off the parent canvas, the edge is
    // rerouted to start at that group's proxy card (a boundary wire) instead of the
    // hidden node. Null when the source node is visible.
    public AssetGraphSubGraph? FromProxy
    {
        get => _fromProxy;
        set
        {
            if (ReferenceEquals(_fromProxy, value))
            {
                return;
            }

            if (_fromProxy is not null)
            {
                _fromProxy.PropertyChanged -= ProxyPositionChanged;
            }

            _fromProxy = value;
            if (_fromProxy is not null)
            {
                _fromProxy.PropertyChanged += ProxyPositionChanged;
            }

            RaiseGeometryChanged();
        }
    }

    // As FromProxy, for the target end of the edge.
    public AssetGraphSubGraph? ToProxy
    {
        get => _toProxy;
        set
        {
            if (ReferenceEquals(_toProxy, value))
            {
                return;
            }

            if (_toProxy is not null)
            {
                _toProxy.PropertyChanged -= ProxyPositionChanged;
            }

            _toProxy = value;
            if (_toProxy is not null)
            {
                _toProxy.PropertyChanged += ProxyPositionChanged;
            }

            RaiseGeometryChanged();
        }
    }

    // Both endpoints collapsed into the same sub-graph — the edge is internal, not drawn.
    public bool IsRenderHidden
    {
        get => _isRenderHidden;
        set => SetProperty(ref _isRenderHidden, value);
    }

    public double StartX => _fromProxy is not null
        ? _fromProxy.ProxyX + AssetGraphSubGraph.ProxyWidth
        : _from.X + _cardWidth;

    public double StartY => _fromProxy is not null
        ? _fromProxy.ProxyY + AssetGraphSubGraph.ConnectorY
        : _from.Y + _portRowBaseY;

    public double EndX => _toProxy is not null
        ? _toProxy.ProxyX
        : _to.X;

    public double EndY => _toProxy is not null
        ? _toProxy.ProxyY + AssetGraphSubGraph.ConnectorY
        : _to.Y + _portRowBaseY + ToInputPort * _portRowSpacing;

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _from.PropertyChanged -= NodePositionChanged;
        _to.PropertyChanged -= NodePositionChanged;
        if (_fromProxy is not null)
        {
            _fromProxy.PropertyChanged -= ProxyPositionChanged;
        }

        if (_toProxy is not null)
        {
            _toProxy.PropertyChanged -= ProxyPositionChanged;
        }

        _disposed = true;
    }

    private void NodePositionChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is not nameof(AssetGraphNodeCardViewModel.X)
            and not nameof(AssetGraphNodeCardViewModel.Y))
        {
            return;
        }

        RaiseGeometryChanged();
    }

    private void ProxyPositionChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is not nameof(AssetGraphSubGraph.ProxyX)
            and not nameof(AssetGraphSubGraph.ProxyY))
        {
            return;
        }

        RaiseGeometryChanged();
    }

    private void RaiseGeometryChanged()
    {
        OnPropertyChanged(nameof(StartX));
        OnPropertyChanged(nameof(StartY));
        OnPropertyChanged(nameof(EndX));
        OnPropertyChanged(nameof(EndY));
    }
}
