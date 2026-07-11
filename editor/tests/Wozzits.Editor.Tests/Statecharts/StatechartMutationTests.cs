using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>Phase-3 mutation: the stateful editor deletes states from the chart + re-projects.</summary>
public sealed class StatechartMutationTests
{
    private static Chart Golden(string file) =>
        StatechartJson.Load(File.ReadAllText(Path.Combine(CorpusLocator.StatechartsDir(), file)));

    private static ControlPaneViewModel Control(string file)
    {
        var pane = new ControlPaneViewModel();
        pane.Project(Golden(file));
        return pane;
    }

    private static DataflowPaneViewModel Dataflow(Chart chart)
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(chart);
        return pane;
    }

    [Fact]
    public void Delete_A_State_Removes_It_And_Every_Transition_Touching_It()
    {
        var pane = Control("traffic_light.sc.json");
        var canvas = (IEditorCanvas)pane;

        canvas.SelectOnly(pane.States.First(s => s.StateId == "HOLD"));
        canvas.DeleteSelected();

        Assert.DoesNotContain(pane.States, s => s.StateId == "HOLD");
        Assert.Single(pane.States);
        Assert.Empty(pane.Transitions);   // DELIBERATE->HOLD and HOLD->DELIBERATE both touched HOLD
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Deleting_The_Initial_State_Repoints_The_Region_Initial()
    {
        var pane = Control("traffic_light.sc.json");
        var canvas = (IEditorCanvas)pane;

        canvas.SelectOnly(pane.States.First(s => s.StateId == "DELIBERATE"));   // region initial
        canvas.DeleteSelected();

        Assert.DoesNotContain(pane.States, s => s.StateId == "DELIBERATE");
        Assert.True(pane.States.Single().IsInitial);   // HOLD became the region's initial
    }

    [Fact]
    public void Deleting_A_Regions_Sole_State_Drops_The_Region()
    {
        var pane = Control("caravan.sc.json");   // 5 regions, one state each
        var canvas = (IEditorCanvas)pane;
        int regionsBefore = pane.Regions.Count;

        canvas.SelectOnly(pane.States.First(s => s.StateId == "S0"));
        canvas.DeleteSelected();

        Assert.Equal(regionsBefore - 1, pane.Regions.Count);
        Assert.DoesNotContain(pane.States, s => s.StateId == "S0");
    }

    [Fact]
    public void Delete_With_No_Selection_Is_A_Noop()
    {
        var pane = Control("traffic_light.sc.json");
        int before = pane.States.Count;

        ((IEditorCanvas)pane).DeleteSelected();

        Assert.Equal(before, pane.States.Count);
        Assert.False(pane.IsDirty);
    }

    // ---- M1: structural creation -------------------------------------------------

    [Fact]
    public void Add_State_Appends_A_State_To_A_Region_And_Selects_It()
    {
        var pane = Control("traffic_light.sc.json");
        int before = pane.States.Count;

        var state = pane.AddState();

        Assert.NotNull(state);
        Assert.Equal(before + 1, pane.States.Count);
        Assert.Same(state, pane.SelectedState);
        Assert.True(pane.IsDirty);
        Assert.Contains(pane.Regions, r => r.StateIds.Contains(state!.StateId));
    }

    [Fact]
    public void Add_State_Into_A_Chart_With_No_Regions_Creates_A_Region()
    {
        var chart = new Chart { Name = "empty" };
        var pane = new ControlPaneViewModel();
        pane.Project(chart);

        var state = pane.AddState();

        Assert.NotNull(state);
        Assert.Single(pane.Regions);
        Assert.True(state!.IsInitial);   // the sole state becomes the new region's initial
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Add_Op_Appends_A_Pure_Op_And_Selects_It()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        int before = pane.Nodes.Count;

        var node = pane.AddOp(OpKind.Mul);

        Assert.NotNull(node);
        Assert.Equal(before + 1, pane.Nodes.Count);
        Assert.Same(node, pane.SelectedNode);
        Assert.True(pane.IsDirty);
        Assert.Contains(chart.Pure, p => p.Id == node!.NodeId && p.Op == OpKind.Mul);
    }

    [Fact]
    public void Add_Op_Produces_A_Valid_Saveable_Chart()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);

        pane.AddOp(OpKind.Select);

        Assert.Empty(StatechartJson.Validate(chart));
        var reloaded = StatechartJson.Load(StatechartJson.Emit(chart, indented: true));
        Assert.Contains(reloaded.Pure, p => p.Op == OpKind.Select);
    }

    [Fact]
    public void Delete_Op_Removes_It_And_Disconnects_Referencing_Effects()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);

        // s0d feeds DELIBERATE's `set_scale lamp_0 = op:s0d`.
        pane.SelectOnly(pane.Nodes.First(n => n.NodeId == "s0d"));
        ((IEditorCanvas)pane).DeleteSelected();

        Assert.DoesNotContain(chart.Pure, p => p.Id == "s0d");
        var effect = chart.States.First(s => s.Id == "DELIBERATE").Do
            .First(e => e.Kind == EffectKind.SetScale && e.TargetBind == "lamp_0");
        Assert.Equal(RefKind.Const, effect.Value!.Kind);   // disconnected to a literal
        Assert.Empty(StatechartJson.Validate(chart));
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Structural_Edit_Preserves_Hand_Placed_Positions()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);

        pane.SelectOnly(pane.Nodes.First(n => n.NodeId == "z"));
        pane.MoveSelectedBy(500, 300);
        double x = pane.Nodes.First(n => n.NodeId == "z").X;
        double y = pane.Nodes.First(n => n.NodeId == "z").Y;

        pane.AddOp(OpKind.Add);   // reprojects the whole canvas

        var moved = pane.Nodes.First(n => n.NodeId == "z");
        Assert.Equal(x, moved.X, 3);
        Assert.Equal(y, moved.Y, 3);
    }
}
