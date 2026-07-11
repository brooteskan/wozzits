namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.ComponentModel;
using System.Globalization;
using Wozzits.Editor.Statecharts;

// A control-layer transition (an arrow between state boxes). Endpoints are box centres;
// the view clips them to the borders and draws the arrowhead, or a loop when self-directed.
public sealed class TransitionViewModel : ViewModelBase, IDisposable
{
    private readonly double _stateWidth;
    private readonly double _stateHeight;
    private bool _disposed;

    public TransitionViewModel(
        Transition model,
        StateNodeViewModel from,
        StateNodeViewModel to,
        double stateWidth,
        double stateHeight)
    {
        Model = model;
        From = from;
        To = to;
        _stateWidth = stateWidth;
        _stateHeight = stateHeight;
        Label = LabelFor(model.Trigger);
        ActionCount = model.Actions.Count;

        From.PropertyChanged += EndpointMoved;
        if (!ReferenceEquals(From, To))
        {
            To.PropertyChanged += EndpointMoved;
        }
    }

    public Transition Model { get; }

    public StateNodeViewModel From { get; }

    public StateNodeViewModel To { get; }

    public string Label { get; }

    public int ActionCount { get; }

    // Index among transitions sharing the same (From, To) ordered pair, assigned by the
    // projection so parallel arrows / stacked self-loops fan out instead of overlapping.
    public int Lane { get; set; }

    public bool IsSelfLoop => ReferenceEquals(From, To);

    public double StartX => From.X + _stateWidth / 2;

    public double StartY => From.Y + _stateHeight / 2;

    public double EndX => To.X + _stateWidth / 2;

    public double EndY => To.Y + _stateHeight / 2;

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        From.PropertyChanged -= EndpointMoved;
        if (!ReferenceEquals(From, To))
        {
            To.PropertyChanged -= EndpointMoved;
        }

        _disposed = true;
    }

    private void EndpointMoved(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is not nameof(StateNodeViewModel.X)
            and not nameof(StateNodeViewModel.Y))
        {
            return;
        }

        OnPropertyChanged(nameof(StartX));
        OnPropertyChanged(nameof(StartY));
        OnPropertyChanged(nameof(EndX));
        OnPropertyChanged(nameof(EndY));
    }

    private static string LabelFor(Trigger t) => t.Kind switch
    {
        TriggerKind.Commit => t.Outcome is int outcome ? $"commit={outcome}" : "commit",
        TriggerKind.After => $"after {t.Seconds.ToString("0.###", CultureInfo.InvariantCulture)}s",
        TriggerKind.Guard => "guard",
        TriggerKind.Event => string.IsNullOrEmpty(t.EventName) ? "event" : $"event {t.EventName}",
        _ => string.Empty,
    };
}
