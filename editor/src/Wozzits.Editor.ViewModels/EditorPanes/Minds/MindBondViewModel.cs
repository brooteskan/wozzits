namespace Wozzits.Editor.ViewModels.EditorPanes.Minds;

using System.ComponentModel;
using Wozzits.Editor.Statecharts;

// A coupling edge between two qubit nodes. Undirected: it attaches to both node centers
// and follows them when dragged (same pattern as DataflowWireViewModel). The sign of j
// picks the colour -- ferromagnetic (agree) vs anti (disagree).
public sealed class MindBondViewModel : ViewModelBase, IDisposable
{
    private bool _disposed;
    private bool _isDimmed;

    public MindBondViewModel(MindNodeViewModel a, MindNodeViewModel b, MindBond model)
    {
        A = a;
        B = b;
        Model = model;
        A.PropertyChanged += NodeMoved;
        B.PropertyChanged += NodeMoved;
    }

    public MindNodeViewModel A { get; }

    public MindNodeViewModel B { get; }

    public MindBond Model { get; }

    public double J => Model.J;

    public bool IsFerromagnetic => Model.J >= 0.0;

    public double StartX => A.CenterX;

    public double StartY => A.CenterY;

    public double EndX => B.CenterX;

    public double EndY => B.CenterY;

    public double MidX => (A.CenterX + B.CenterX) / 2.0;

    public double MidY => (A.CenterY + B.CenterY) / 2.0;

    public bool IsDimmed
    {
        get => _isDimmed;
        set => SetProperty(ref _isDimmed, value);
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        A.PropertyChanged -= NodeMoved;
        B.PropertyChanged -= NodeMoved;
        _disposed = true;
    }

    private void NodeMoved(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is not nameof(MindNodeViewModel.X)
            and not nameof(MindNodeViewModel.Y))
        {
            return;
        }

        OnPropertyChanged(nameof(StartX));
        OnPropertyChanged(nameof(StartY));
        OnPropertyChanged(nameof(EndX));
        OnPropertyChanged(nameof(EndY));
        OnPropertyChanged(nameof(MidX));
        OnPropertyChanged(nameof(MidY));
    }
}
