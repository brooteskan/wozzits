namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.ComponentModel;

// A dataflow wire: a source node's single output -> a specific input row on the target.
// Endpoints track the two nodes' positions (same geometry the asset-graph canvas uses),
// so wires follow their cards when dragged.
public sealed class DataflowWireViewModel : ViewModelBase, IDisposable
{
    private readonly double _cardWidth;
    private readonly double _portRowBaseY;
    private readonly double _portRowSpacing;
    private bool _disposed;
    private bool _isDimmed;

    public DataflowWireViewModel(
        DataflowNodeViewModel from,
        DataflowNodeViewModel to,
        int toInputIndex,
        double cardWidth,
        double portRowBaseY,
        double portRowSpacing)
    {
        From = from;
        To = to;
        ToInputIndex = toInputIndex;
        _cardWidth = cardWidth;
        _portRowBaseY = portRowBaseY;
        _portRowSpacing = portRowSpacing;

        From.PropertyChanged += NodeMoved;
        To.PropertyChanged += NodeMoved;
    }

    public DataflowNodeViewModel From { get; }

    public DataflowNodeViewModel To { get; }

    public int ToInputIndex { get; }

    public bool IsDimmed
    {
        get => _isDimmed;
        set => SetProperty(ref _isDimmed, value);
    }

    public double StartX => From.X + _cardWidth;

    public double StartY => From.Y + _portRowBaseY;

    public double EndX => To.X;

    public double EndY => To.Y + _portRowBaseY + ToInputIndex * _portRowSpacing;

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        From.PropertyChanged -= NodeMoved;
        To.PropertyChanged -= NodeMoved;
        _disposed = true;
    }

    private void NodeMoved(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is not nameof(DataflowNodeViewModel.X)
            and not nameof(DataflowNodeViewModel.Y))
        {
            return;
        }

        OnPropertyChanged(nameof(StartX));
        OnPropertyChanged(nameof(StartY));
        OnPropertyChanged(nameof(EndX));
        OnPropertyChanged(nameof(EndY));
    }
}
