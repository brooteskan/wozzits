namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.ComponentModel;

// A dataflow wire: a source node's single output -> a specific input row on the target.
// Endpoints track the two nodes' positions (same geometry the asset-graph canvas uses),
// so wires follow their cards when dragged.
public sealed class DataflowWireViewModel : ViewModelBase, IDisposable
{
    private bool _disposed;
    private bool _isDimmed;

    public DataflowWireViewModel(
        DataflowNodeViewModel from,
        DataflowNodeViewModel to,
        int toInputIndex)
    {
        From = from;
        To = to;
        ToInputIndex = toInputIndex;

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

    public double StartX => From.X + From.OutputDotX;

    public double StartY => From.Y + From.OutputDotY;

    public double EndX => To.X + To.InputDotX;

    public double EndY => To.Y + To.InputDotBaseY + ToInputIndex * DataflowPaneViewModel.PortRowSpacing;

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
            and not nameof(DataflowNodeViewModel.Y)
            and not DataflowNodeViewModel.PortGeometryChanged)
        {
            return;
        }

        OnPropertyChanged(nameof(StartX));
        OnPropertyChanged(nameof(StartY));
        OnPropertyChanged(nameof(EndX));
        OnPropertyChanged(nameof(EndY));
    }
}
