namespace Wozzits.Editor.ViewModels.EditorPanes.Minds;

using System;
using System.ComponentModel;
using System.Globalization;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// A coupling edge between two qubit nodes. Undirected: it attaches to both node centers
// and follows them when dragged (same pattern as DataflowWireViewModel). The sign of j
// picks the colour -- ferromagnetic (agree) vs anti (disagree). j is editable + the bond
// removable from the document's properties panel.
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
        JEditor = new EditableFieldViewModel(
            "j",
            () => Model.J.ToString("0.###", CultureInfo.InvariantCulture),
            v =>
            {
                if (double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out var j)
                    && j != Model.J)
                {
                    Model.J = j;
                    OnPropertyChanged(nameof(J));
                    OnPropertyChanged(nameof(IsFerromagnetic));  // the bond layer repaints
                }
            },
            () => JEdited?.Invoke());
        RemoveCommand = new RelayCommand(() => RemoveRequested?.Invoke());
    }

    public MindNodeViewModel A { get; }

    public MindNodeViewModel B { get; }

    public MindBond Model { get; }

    public double J => Model.J;

    public bool IsFerromagnetic => Model.J >= 0.0;

    // "q0 ↔ q1" -- the two endpoints, for the bond row in the properties panel.
    public string Title => $"{A.Title} ↔ {B.Title}";

    // Edit the coupling strength/sign (repaints the edge); fires JEdited on commit.
    public EditableFieldViewModel JEditor { get; }

    public Action? JEdited { get; set; }

    public IRelayCommand RemoveCommand { get; }

    public Action? RemoveRequested { get; set; }

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
