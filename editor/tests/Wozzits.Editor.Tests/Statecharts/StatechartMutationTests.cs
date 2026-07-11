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
}
