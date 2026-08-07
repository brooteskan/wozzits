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
        NameEditor = new EditableFieldViewModel("name", () => RegionId, v => RenameRequested?.Invoke(v));
    }

    public string RegionId { get; }

    public string Title => RegionId;

    // Editable region name (its id). Nothing else in the chart references a region id, so a
    // rename is just the id -- the pane does it (RenameRegion) via RenameRequested, bound to the
    // swimlane's editable header. Regions are orthogonal: one active state each, stepping together.
    public EditableFieldViewModel NameEditor { get; }

    public Action<string>? RenameRequested { get; set; }

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
