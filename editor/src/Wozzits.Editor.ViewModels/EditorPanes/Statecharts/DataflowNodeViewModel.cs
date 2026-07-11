namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.Collections.ObjectModel;

public enum DataflowNodeKind
{
    Binding,
    Agent,
    Op,
}

// A card on the dataflow canvas: a binding, an agent, or a pure op. `Model` is the
// backing authoring object (Binding | AgentDecl | PureOp) for editing/inspection.
public sealed class DataflowNodeViewModel : ViewModelBase, ICanvasNode
{
    private double _x;
    private double _y;
    private bool _isSelected;

    public DataflowNodeViewModel(DataflowNodeKind kind, string nodeId, string subtitle, object model)
    {
        Kind = kind;
        NodeId = nodeId;
        Subtitle = subtitle;
        Model = model;
    }

    public DataflowNodeKind Kind { get; }

    public string NodeId { get; }

    public string Title => NodeId;

    public string Subtitle { get; }

    public object Model { get; }

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

    public bool IsSelected
    {
        get => _isSelected;
        set => SetProperty(ref _isSelected, value);
    }

    // Assigned by the pane's auto-layout: column == dependency depth, row == order within.
    public int Column { get; set; }

    public int Row { get; set; }

    public ObservableCollection<DataflowPortViewModel> InputPorts { get; } = [];

    public ObservableCollection<DataflowPortViewModel> OutputPorts { get; } = [];

    public bool HasInputPorts => InputPorts.Count > 0;
}
