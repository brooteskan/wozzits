namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// A control-layer region drawn as a titled swimlane enclosing its states. v0 regions are
// flat and orthogonal (they step concurrently); nesting is a future IR extension.
public sealed class RegionViewModel : ViewModelBase
{
    private double _x;
    private double _y;
    private double _width;
    private double _height;

    public RegionViewModel(string regionId, IReadOnlyList<string> stateIds)
    {
        RegionId = regionId;
        StateIds = stateIds;
    }

    public string RegionId { get; }

    public string Title => RegionId;

    public IReadOnlyList<string> StateIds { get; }

    public double X
    {
        get => _x;
        set => SetProperty(ref _x, value);
    }

    public double Y
    {
        get => _y;
        set => SetProperty(ref _y, value);
    }

    public double Width
    {
        get => _width;
        set => SetProperty(ref _width, value);
    }

    public double Height
    {
        get => _height;
        set => SetProperty(ref _height, value);
    }
}
