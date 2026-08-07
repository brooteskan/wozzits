using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>Phase-1 editing: selection / move / zoom logic behind the shared canvas controller.</summary>
public sealed class StatechartCanvasInteractionTests
{
    private static Chart Golden(string file) =>
        StatechartJson.Load(File.ReadAllText(Path.Combine(CorpusLocator.StatechartsDir(), file)));

    private static DataflowPaneViewModel Dataflow(string file)
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(Golden(file));
        return pane;
    }

    [Fact]
    public void SelectOnly_Selects_A_Single_Node()
    {
        var pane = Dataflow("traffic_light.sc.json");
        ((IEditorCanvas)pane).SelectOnly(pane.Nodes[0]);

        Assert.True(pane.Nodes[0].IsSelected);
        Assert.Single(pane.SelectedNodes);
        Assert.Equal(1, pane.Nodes.Count(n => n.IsSelected));
    }

    [Fact]
    public void ToggleSelection_Adds_Then_Removes()
    {
        var pane = Dataflow("traffic_light.sc.json");
        var canvas = (IEditorCanvas)pane;

        canvas.SelectOnly(pane.Nodes[0]);
        canvas.ToggleSelection(pane.Nodes[1]);
        Assert.Equal(2, pane.SelectedNodes.Count);

        canvas.ToggleSelection(pane.Nodes[1]);
        Assert.Single(pane.SelectedNodes);
        Assert.False(pane.Nodes[1].IsSelected);
    }

    [Fact]
    public void SelectInRectangle_Grabs_Overlapping_Nodes()
    {
        var pane = Dataflow("traffic_light.sc.json");
        ((IEditorCanvas)pane).SelectInRectangle(-10, -10, pane.GraphWidth + 10, pane.GraphHeight + 10, additive: false);

        Assert.Equal(pane.Nodes.Count, pane.SelectedNodes.Count);
    }

    [Fact]
    public void SelectInRectangle_NonAdditive_Replaces_Selection()
    {
        var pane = Dataflow("traffic_light.sc.json");
        var canvas = (IEditorCanvas)pane;

        canvas.SelectOnly(pane.Nodes[0]);
        // an empty far-away rectangle, non-additive, clears the previous selection
        canvas.SelectInRectangle(100000, 100000, 100010, 100010, additive: false);
        Assert.Empty(pane.SelectedNodes);
    }

    [Fact]
    public void MoveSelectedBy_Shifts_Selected_And_Clamps_At_Zero()
    {
        var pane = Dataflow("traffic_light.sc.json");
        var canvas = (IEditorCanvas)pane;
        var node = pane.Nodes[0];
        canvas.SelectOnly(node);

        double x0 = node.X;
        double y0 = node.Y;
        canvas.MoveSelectedBy(15, 25);
        Assert.Equal(x0 + 15, node.X, 3);
        Assert.Equal(y0 + 25, node.Y, 3);

        canvas.MoveSelectedBy(-100000, -100000);
        Assert.Equal(0, node.X, 3);
        Assert.Equal(0, node.Y, 3);
    }

    [Fact]
    public void MoveSelectedBy_Leaves_Unselected_Nodes_Put()
    {
        var pane = Dataflow("traffic_light.sc.json");
        var canvas = (IEditorCanvas)pane;
        canvas.SelectOnly(pane.Nodes[0]);

        var other = pane.Nodes[1];
        double ox = other.X;
        double oy = other.Y;
        canvas.MoveSelectedBy(40, 40);
        Assert.Equal(ox, other.X, 3);
        Assert.Equal(oy, other.Y, 3);
    }

    [Fact]
    public void ZoomByWheel_Saturates_At_The_Range_Ends()
    {
        var pane = Dataflow("traffic_light.sc.json");
        var canvas = (IEditorCanvas)pane;

        for (int i = 0; i < 60; i++) canvas.ZoomByWheel(+1);
        Assert.Equal(4.0, pane.Zoom, 6);

        for (int i = 0; i < 120; i++) canvas.ZoomByWheel(-1);
        Assert.Equal(0.25, pane.Zoom, 6);
    }

    [Fact]
    public void Control_Canvas_Supports_Selection_And_Move_Too()
    {
        var pane = new ControlPaneViewModel();
        pane.Project(Golden("traffic_light.sc.json"));
        var canvas = (IEditorCanvas)pane;

        var state = pane.States[0];
        canvas.SelectOnly(state);
        Assert.True(state.IsSelected);
        Assert.Single(pane.SelectedStates);

        double x0 = state.X;
        canvas.MoveSelectedBy(30, 0);
        Assert.Equal(x0 + 30, state.X, 3);
    }

    [Fact]
    public void Moving_A_State_Keeps_Its_Region_Swimlane_Enclosing_It()
    {
        var pane = new ControlPaneViewModel();
        pane.Project(Golden("caravan.sc.json"));
        var canvas = (IEditorCanvas)pane;

        var state = pane.States[0];
        canvas.SelectOnly(state);
        canvas.MoveSelectedBy(220, 140);   // drag it well away from where it started

        var region = pane.Regions.First(r => r.StateIds.Contains(state.StateId));
        Assert.True(state.X >= region.X - 0.01, "state escaped left of its region");
        Assert.True(state.Y >= region.Y - 0.01, "state escaped above its region");
        Assert.True(state.X + ControlPaneViewModel.StateWidth <= region.X + region.Width + 0.01, "state escaped right of its region");
        Assert.True(state.Y + ControlPaneViewModel.StateHeight <= region.Y + region.Height + 0.01, "state escaped below its region");
    }
}
