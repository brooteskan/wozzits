namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Collections.ObjectModel;
using Wozzits.Editor.Statecharts;

/// <summary>
/// Projects a statechart's CONTROL layer (regions, states, transitions) onto a canvas.
/// Each region is a titled swimlane (stacked vertically); its states lay out in a row;
/// transitions are arrows between state boxes (self-directed ones become loops). Unlike
/// the acyclic dataflow layer this layer is cyclic, so there is no left-to-right layering.
/// Positions are transient here; hand-placed layout persistence comes with the view.
/// </summary>
public sealed class ControlPaneViewModel : ViewModelBase
{
    public const double StateWidth = 180.0;
    public const double StateHeight = 76.0;

    private const double CanvasPadding = 28.0;
    private const double StateGapX = 90.0;
    private const double RegionPadding = 18.0;
    private const double RegionHeaderHeight = 26.0;
    private const double RegionGapY = 36.0;
    private const double MinZoom = 0.25;
    private const double MaxZoom = 4.0;

    private double _zoom = 1.0;

    public ObservableCollection<RegionViewModel> Regions { get; } = [];

    public ObservableCollection<StateNodeViewModel> States { get; } = [];

    public ObservableCollection<TransitionViewModel> Transitions { get; } = [];

    public ObservableCollection<StateNodeViewModel> SelectedStates { get; } = [];

    public bool HasGraph => States.Count > 0;

    public double Zoom
    {
        get => _zoom;
        set
        {
            if (SetProperty(ref _zoom, Math.Clamp(value, MinZoom, MaxZoom)))
            {
                OnPropertyChanged(nameof(ScaledGraphWidth));
                OnPropertyChanged(nameof(ScaledGraphHeight));
            }
        }
    }

    public double GraphWidth => Math.Max(
        Regions.Count == 0 ? 0 : Regions.Max(r => r.X + r.Width),
        States.Count == 0 ? 0 : States.Max(s => s.X + StateWidth)) + CanvasPadding;

    public double GraphHeight => Math.Max(
        Regions.Count == 0 ? 0 : Regions.Max(r => r.Y + r.Height),
        States.Count == 0 ? 0 : States.Max(s => s.Y + StateHeight)) + CanvasPadding;

    public double ScaledGraphWidth => GraphWidth * Zoom;

    public double ScaledGraphHeight => GraphHeight * Zoom;

    /// <summary>Rebuild the canvas from a chart's control layer.</summary>
    public void Project(Chart chart)
    {
        foreach (var t in Transitions)
        {
            t.Dispose();
        }

        Transitions.Clear();
        States.Clear();
        Regions.Clear();
        SelectedStates.Clear();

        var initials = new HashSet<string>();
        foreach (var r in chart.Regions)
        {
            initials.Add(r.Initial);
        }

        var stateVms = new Dictionary<string, StateNodeViewModel>();
        foreach (var s in chart.States)
        {
            stateVms[s.Id] = new StateNodeViewModel(s, initials.Contains(s.Id));
        }

        // Regions as vertical swimlanes; each region's states in a row.
        double top = CanvasPadding;
        var placed = new HashSet<string>();
        foreach (var r in chart.Regions)
        {
            var region = new RegionViewModel(r.Id, r.States.ToList());
            int column = 0;
            foreach (var sid in r.States)
            {
                if (!stateVms.TryGetValue(sid, out var vm))
                {
                    continue;
                }

                vm.X = CanvasPadding + RegionPadding + column * (StateWidth + StateGapX);
                vm.Y = top + RegionHeaderHeight + RegionPadding;
                placed.Add(sid);
                column++;
            }

            int count = Math.Max(1, column);
            region.X = CanvasPadding;
            region.Y = top;
            region.Width = RegionPadding * 2 + count * StateWidth + (count - 1) * StateGapX;
            region.Height = RegionHeaderHeight + RegionPadding * 2 + StateHeight;
            Regions.Add(region);
            top += region.Height + RegionGapY;
        }

        // Any state not claimed by a region: a trailing row (defensive; goldens are 1:1).
        double orphanX = CanvasPadding;
        foreach (var s in chart.States)
        {
            if (placed.Contains(s.Id))
            {
                continue;
            }

            var vm = stateVms[s.Id];
            vm.X = orphanX;
            vm.Y = top + RegionPadding;
            orphanX += StateWidth + StateGapX;
        }

        foreach (var s in chart.States)
        {
            States.Add(stateVms[s.Id]);
        }

        foreach (var s in chart.States)
        {
            var from = stateVms[s.Id];
            foreach (var transition in s.Transitions)
            {
                if (stateVms.TryGetValue(transition.Target, out var to))
                {
                    Transitions.Add(new TransitionViewModel(transition, from, to, StateWidth, StateHeight));
                }
            }
        }

        OnPropertyChanged(nameof(HasGraph));
        OnPropertyChanged(nameof(GraphWidth));
        OnPropertyChanged(nameof(GraphHeight));
        OnPropertyChanged(nameof(ScaledGraphWidth));
        OnPropertyChanged(nameof(ScaledGraphHeight));
    }
}
