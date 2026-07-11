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
public sealed class ControlPaneViewModel : ViewModelBase, IEditorCanvas
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

    private readonly Dictionary<string, StateNodeViewModel> _statesById = new();

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

    public IReadOnlyList<ICanvasNode> CanvasNodes => States;

    public void SelectOnly(ICanvasNode node)
    {
        foreach (var s in States)
        {
            s.IsSelected = false;
        }

        SelectedStates.Clear();
        if (node is StateNodeViewModel state)
        {
            state.IsSelected = true;
            SelectedStates.Add(state);
        }
    }

    public void ToggleSelection(ICanvasNode node)
    {
        if (node is not StateNodeViewModel state)
        {
            return;
        }

        state.IsSelected = !state.IsSelected;
        if (state.IsSelected)
        {
            if (!SelectedStates.Contains(state))
            {
                SelectedStates.Add(state);
            }
        }
        else
        {
            SelectedStates.Remove(state);
        }
    }

    public void ClearSelection()
    {
        foreach (var s in States)
        {
            s.IsSelected = false;
        }

        SelectedStates.Clear();
    }

    public void SelectInRectangle(double x0, double y0, double x1, double y1, bool additive)
    {
        double left = Math.Min(x0, x1);
        double right = Math.Max(x0, x1);
        double top = Math.Min(y0, y1);
        double bottom = Math.Max(y0, y1);

        if (!additive)
        {
            ClearSelection();
        }

        foreach (var s in States)
        {
            bool overlaps = s.X < right && s.X + StateWidth > left && s.Y < bottom && s.Y + StateHeight > top;
            if (overlaps && !s.IsSelected)
            {
                s.IsSelected = true;
                SelectedStates.Add(s);
            }
        }
    }

    public void MoveSelectedBy(double dx, double dy)
    {
        foreach (var s in SelectedStates)
        {
            s.X = Math.Max(0, s.X + dx);
            s.Y = Math.Max(0, s.Y + dy);
        }

        RecomputeRegionBounds();
        RaiseExtentChanged();
    }

    public void ZoomByWheel(double wheelDelta)
    {
        Zoom = wheelDelta > 0 ? Zoom * 1.1 : Zoom / 1.1;
    }

    private void RaiseExtentChanged()
    {
        OnPropertyChanged(nameof(GraphWidth));
        OnPropertyChanged(nameof(GraphHeight));
        OnPropertyChanged(nameof(ScaledGraphWidth));
        OnPropertyChanged(nameof(ScaledGraphHeight));
    }

    // Each region's swimlane wraps its member states (+ header + padding), so a state box
    // never escapes its container -- drag a state and its region grows to follow it.
    private void RecomputeRegionBounds()
    {
        foreach (var region in Regions)
        {
            double minX = double.MaxValue;
            double minY = double.MaxValue;
            double maxX = double.MinValue;
            double maxY = double.MinValue;
            int count = 0;
            foreach (var stateId in region.StateIds)
            {
                if (!_statesById.TryGetValue(stateId, out var s))
                {
                    continue;
                }

                minX = Math.Min(minX, s.X);
                minY = Math.Min(minY, s.Y);
                maxX = Math.Max(maxX, s.X + StateWidth);
                maxY = Math.Max(maxY, s.Y + StateHeight);
                count++;
            }

            if (count == 0)
            {
                continue;
            }

            region.X = Math.Max(0, minX - RegionPadding);
            region.Y = Math.Max(0, minY - RegionHeaderHeight - RegionPadding);
            region.Width = (maxX - Math.Max(0, minX - RegionPadding)) + RegionPadding;
            region.Height = (maxY - Math.Max(0, minY - RegionHeaderHeight - RegionPadding)) + RegionPadding;
        }
    }

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

        var stateVms = _statesById;
        stateVms.Clear();
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

            Regions.Add(region);
            top += RegionHeaderHeight + RegionPadding * 2 + StateHeight + RegionGapY;
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

        RecomputeRegionBounds();

        OnPropertyChanged(nameof(HasGraph));
        OnPropertyChanged(nameof(GraphWidth));
        OnPropertyChanged(nameof(GraphHeight));
        OnPropertyChanged(nameof(ScaledGraphWidth));
        OnPropertyChanged(nameof(ScaledGraphHeight));
    }
}
