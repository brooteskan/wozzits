namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.Statecharts;

/// <summary>
/// Projects a statechart's CONTROL layer (regions, states, transitions) onto a canvas.
/// Each region is a titled swimlane (stacked vertically); its states lay out in a row;
/// transitions are arrows between state boxes (self-directed ones become loops). Unlike
/// the acyclic dataflow layer this layer is cyclic, so there is no left-to-right layering.
/// Positions are transient here; hand-placed layout persistence comes with the view.
/// </summary>
public sealed class ControlPaneViewModel : ViewModelBase, IEditorCanvas, ITransitionDrawCanvas
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
    private StateNodeViewModel? _selectedState;
    private Chart? _chart;
    private bool _isDirty;
    private bool _isLayoutDirty;
    private bool _isDrawingTransition;

    private readonly Dictionary<string, StateNodeViewModel> _statesById = new();

    public ControlPaneViewModel()
    {
        SelectedStates.CollectionChanged += (_, _) => UpdateSelectedState();
        AddStateCommand = new RelayCommand(() => AddState());
        DeleteSelectedCommand = new RelayCommand(DeleteSelected);
    }

    // Toolbar: add a state to the selected state's region; delete the current selection.
    public IRelayCommand AddStateCommand { get; }

    public IRelayCommand DeleteSelectedCommand { get; }

    // When armed (a two-way-bound toolbar toggle), a drag from one state to another authors a
    // transition instead of moving the state; the interaction controller disarms after each
    // attempt, which the toggle reflects.
    public bool IsDrawingTransition
    {
        get => _isDrawingTransition;
        set => SetProperty(ref _isDrawingTransition, value);
    }

    public void DisarmTransitionDraw() => IsDrawingTransition = false;

    // Author a transition from -> to (self-drop = self-loop), defaulting to an editable
    // `after 1s` trigger (the user retargets/retriggers it in the inspector). Reprojects.
    public bool TryAddTransition(string fromStateId, string toStateId)
    {
        if (_chart is null)
        {
            return false;
        }

        var from = _chart.States.FirstOrDefault(s => s.Id == fromStateId);
        if (from is null || _chart.States.All(s => s.Id != toStateId))
        {
            return false;
        }

        from.Transitions.Add(new Transition
        {
            Target = toStateId,
            Trigger = new Trigger { Kind = TriggerKind.After, Seconds = 1.0 },
        });

        IsDirty = true;
        ReprojectPreservingSelection();
        return true;
    }

    // Make a state its region's initial (the marked start of that orthogonal region). Reprojects,
    // keeping the state selected so the inspector's initial marker updates.
    public void SetInitial(State state)
    {
        if (_chart is null)
        {
            return;
        }

        var region = _chart.Regions.FirstOrDefault(r => r.States.Contains(state.Id));
        if (region is null || region.Initial == state.Id)
        {
            return;
        }

        region.Initial = state.Id;
        IsDirty = true;
        ReprojectPreservingSelection();
    }

    // Add an effect of the given kind to a state's do/entry/exit list, seeded with defaults
    // (agent effects target the first agent; actuators target the first binding; values are
    // constants the user then edits). Reprojects, keeping the state selected.
    public void AddEffect(State owner, EffectSlot slot, EffectKind kind)
    {
        if (_chart is null)
        {
            return;
        }

        var list = slot switch
        {
            EffectSlot.Entry => owner.Entry,
            EffectSlot.Exit => owner.Exit,
            _ => owner.Do,
        };
        list.Add(NewEffect(kind, _chart));

        IsDirty = true;
        ReprojectPreservingSelection();
    }

    // Remove an effect from whichever of the state's lists holds it.
    public void DeleteEffect(State owner, Effect effect)
    {
        if (_chart is null)
        {
            return;
        }

        if (owner.Do.Remove(effect) || owner.Entry.Remove(effect) || owner.Exit.Remove(effect))
        {
            IsDirty = true;
            ReprojectPreservingSelection();
        }
    }

    private static Effect NewEffect(EffectKind kind, Chart chart)
    {
        string agent = chart.Agents.Count > 0 ? chart.Agents[0].Id : string.Empty;
        string bind = chart.Bindings.Count > 0 ? chart.Bindings[0].Port : string.Empty;
        return kind switch
        {
            EffectKind.SetGoal => new Effect { Kind = kind, Agent = agent, Slot = 0, Value = ValueRef.Number(0) },
            EffectKind.SetDecoherence => new Effect { Kind = kind, Agent = agent, Value = ValueRef.Number(0) },
            EffectKind.Rearm => new Effect { Kind = kind, Agent = agent },
            EffectKind.Reward => new Effect { Kind = kind, Agent = agent, Slot = 0, Value = ValueRef.Number(0) },
            EffectKind.SetScale => new Effect { Kind = kind, TargetBind = bind, Value = ValueRef.Number(1) },
            EffectKind.SetVisible => new Effect { Kind = kind, TargetBind = bind, Value = ValueRef.Bool(true) },
            EffectKind.PlaySound => new Effect { Kind = kind, TargetBind = bind },
            _ => new Effect { Kind = kind },
        };
    }

    // Remove a specific outgoing transition (from the inspector's transition list). Keeps the
    // owning state selected so the inspector refreshes to the shortened list.
    public void DeleteTransition(State owner, Transition transition)
    {
        if (_chart is null || !owner.Transitions.Remove(transition))
        {
            return;
        }

        IsDirty = true;
        ReprojectPreservingSelection();
    }

    // Change a transition's trigger kind, seeding the new kind's fields with sensible defaults
    // (commit → the first agent, fires on any outcome; after → 1s; guard → an always-true cond).
    // Reprojects so the arrow label + inspector row reflect the new kind.
    public void SetTriggerKind(State owner, Transition transition, TriggerKind kind)
    {
        if (_chart is null || !owner.Transitions.Contains(transition))
        {
            return;
        }

        var trigger = transition.Trigger;
        if (trigger.Kind == kind)
        {
            return;
        }

        trigger.Kind = kind;
        switch (kind)
        {
            case TriggerKind.Commit:
                if (string.IsNullOrEmpty(trigger.Agent))
                {
                    trigger.Agent = _chart.Agents.Count > 0 ? _chart.Agents[0].Id : string.Empty;
                }
                trigger.Outcome = null;   // any outcome
                break;
            case TriggerKind.After:
                if (trigger.Seconds <= 0)
                {
                    trigger.Seconds = 1.0;
                }
                break;
            case TriggerKind.Guard:
                trigger.Cond ??= ValueRef.Number(1);   // valid, not permanently dead
                break;
            case TriggerKind.Event:
                break;   // EventName kept as-is
        }

        IsDirty = true;
        ReprojectPreservingSelection();
    }

    public ObservableCollection<RegionViewModel> Regions { get; } = [];

    public ObservableCollection<StateNodeViewModel> States { get; } = [];

    public ObservableCollection<TransitionViewModel> Transitions { get; } = [];

    public ObservableCollection<StateNodeViewModel> SelectedStates { get; } = [];

    public bool HasGraph => States.Count > 0;

    public StateNodeViewModel? SelectedState
    {
        get => _selectedState;
        private set
        {
            if (SetProperty(ref _selectedState, value))
            {
                OnPropertyChanged(nameof(HasSelectedState));
            }
        }
    }

    public bool HasSelectedState => _selectedState is not null;

    public bool IsDirty
    {
        get => _isDirty;
        private set => SetProperty(ref _isDirty, value);
    }

    public bool IsLayoutDirty
    {
        get => _isLayoutDirty;
        private set => SetProperty(ref _isLayoutDirty, value);
    }

    public void ClearDirty()
    {
        IsDirty = false;
        IsLayoutDirty = false;
    }

    // A state's effect/transition field was edited in the inspector: the chart changed
    // in place (no reproject), so mark it dirty for save.
    public void MarkChartDirty() => IsDirty = true;

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
        IsLayoutDirty = true;
    }

    public void ZoomByWheel(double wheelDelta)
    {
        Zoom = wheelDelta > 0 ? Zoom * 1.1 : Zoom / 1.1;
        IsLayoutDirty = true;
    }

    private void RaiseExtentChanged()
    {
        OnPropertyChanged(nameof(GraphWidth));
        OnPropertyChanged(nameof(GraphHeight));
        OnPropertyChanged(nameof(ScaledGraphWidth));
        OnPropertyChanged(nameof(ScaledGraphHeight));
    }

    private void UpdateSelectedState() =>
        SelectedState = SelectedStates.Count == 1 ? SelectedStates[0] : null;

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

    // Add an empty state to the selected state's region (else the first region, else a fresh
    // one), make it the region's initial if the region had none, select it. Returns the new
    // state (null if no chart).
    public StateNodeViewModel? AddState()
    {
        if (_chart is null)
        {
            return null;
        }

        var region = RegionForNewState();
        var id = FreshId("state", _chart.States.Select(s => s.Id));
        _chart.States.Add(new State { Id = id });
        region.States.Add(id);
        if (string.IsNullOrEmpty(region.Initial))
        {
            region.Initial = id;
        }

        IsDirty = true;
        ReprojectPreservingLayout();

        var vm = States.FirstOrDefault(s => s.StateId == id);
        if (vm is not null)
        {
            SelectOnly(vm);
        }

        return vm;
    }

    // Where a new state lands: the selected state's region, else the first region, else a new
    // region created to hold it (a chart must have at least one region).
    private Region RegionForNewState()
    {
        if (_selectedState is not null)
        {
            var owner = _chart!.Regions.FirstOrDefault(r => r.States.Contains(_selectedState.StateId));
            if (owner is not null)
            {
                return owner;
            }
        }

        if (_chart!.Regions.Count > 0)
        {
            return _chart.Regions[0];
        }

        var region = new Region { Id = FreshId("region", _chart.Regions.Select(r => r.Id)) };
        _chart.Regions.Add(region);
        return region;
    }

    private static string FreshId(string prefix, IEnumerable<string> taken)
    {
        var used = taken.ToHashSet();
        for (int i = 1; ; i++)
        {
            var candidate = prefix + i;
            if (!used.Contains(candidate))
            {
                return candidate;
            }
        }
    }

    public void DeleteSelected()
    {
        if (_chart is null || SelectedStates.Count == 0)
        {
            return;
        }

        var doomed = SelectedStates.Select(s => s.StateId).ToHashSet();
        _chart.States.RemoveAll(s => doomed.Contains(s.Id));
        foreach (var region in _chart.Regions)
        {
            region.States.RemoveAll(doomed.Contains);
            if (doomed.Contains(region.Initial))
            {
                region.Initial = region.States.Count > 0 ? region.States[0] : string.Empty;
            }
        }

        _chart.Regions.RemoveAll(r => r.States.Count == 0);
        foreach (var state in _chart.States)
        {
            state.Transitions.RemoveAll(t => doomed.Contains(t.Target));
        }

        IsDirty = true;
        ReprojectPreservingLayout();
    }

    // Snapshot the current state positions + zoom so a structural reproject can restore hand
    // placement (surviving states keep their spot; a new state keeps its auto-layout column).
    public StatechartLayout CaptureLayout()
    {
        var layout = new StatechartLayout { ControlZoom = Zoom };
        foreach (var state in States)
        {
            layout.StatePositions[state.StateId] = new StatechartLayout.Point(state.X, state.Y);
        }

        return layout;
    }

    private void ReprojectPreservingLayout()
    {
        var layout = CaptureLayout();
        Project(_chart!);
        ApplyLayout(layout);
    }

    // As above, but re-select the previously selected state by id after the rebuild, so an
    // inspector-driven edit (add/delete transition) keeps its selection and refreshes.
    private void ReprojectPreservingSelection()
    {
        var selectedId = _selectedState?.StateId;
        ReprojectPreservingLayout();
        if (selectedId is not null && States.FirstOrDefault(s => s.StateId == selectedId) is { } again)
        {
            SelectOnly(again);
        }
    }

    // Apply a saved layout (zoom + hand-placed state positions) over the auto-layout. This is
    // a restore, not an edit, so it does not mark the pane dirty.
    public void ApplyLayout(StatechartLayout layout)
    {
        Zoom = layout.ControlZoom;
        foreach (var state in States)
        {
            if (layout.StatePositions.TryGetValue(state.StateId, out var p))
            {
                state.X = p.X;
                state.Y = p.Y;
            }
        }

        RecomputeRegionBounds();
        RaiseExtentChanged();
    }

    /// <summary>Rebuild the canvas from a chart's control layer.</summary>
    public void Project(Chart chart)
    {
        _chart = chart;

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
            stateVms[s.Id] = new StateNodeViewModel(s, initials.Contains(s.Id))
            {
                Edited = MarkChartDirty,
                TransitionDeleteRequested = t => DeleteTransition(s, t),
                TransitionKindChangeRequested = (t, k) => SetTriggerKind(s, t, k),
                EffectAddRequested = (slot, kind) => AddEffect(s, slot, kind),
                EffectDeleteRequested = e => DeleteEffect(s, e),
                SetInitialRequested = () => SetInitial(s),
            };
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

        // Fan parallel transitions: each edge gets a lane index within its (from, to) pair so
        // same-direction arrows and stacked self-loops separate instead of drawing on top.
        var laneByPair = new Dictionary<(string From, string To), int>();
        foreach (var s in chart.States)
        {
            var from = stateVms[s.Id];
            foreach (var transition in s.Transitions)
            {
                if (stateVms.TryGetValue(transition.Target, out var to))
                {
                    var key = (s.Id, transition.Target);
                    int lane = laneByPair.TryGetValue(key, out var n) ? n : 0;
                    laneByPair[key] = lane + 1;
                    Transitions.Add(new TransitionViewModel(transition, from, to, StateWidth, StateHeight) { Lane = lane });
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
