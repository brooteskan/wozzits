using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>E3a: the control-layer projection (Chart -> region/state/transition view-models).</summary>
public sealed class StatechartControlProjectionTests
{
    private static Chart Golden(string file) =>
        StatechartJson.Load(File.ReadAllText(Path.Combine(CorpusLocator.StatechartsDir(), file)));

    [Fact]
    public void TrafficLight_Projects_Region_States_And_Transitions()
    {
        var pane = new ControlPaneViewModel();
        pane.Project(Golden("traffic_light.sc.json"));

        Assert.Single(pane.Regions);
        Assert.Equal(2, pane.States.Count);
        Assert.Equal(2, pane.Transitions.Count);
        Assert.True(pane.HasGraph);

        var delib = pane.States.First(s => s.StateId == "DELIBERATE");
        var hold = pane.States.First(s => s.StateId == "HOLD");
        Assert.True(delib.IsInitial);
        Assert.False(hold.IsInitial);

        Assert.Contains(pane.Transitions, t => t.From == delib && t.To == hold && t.Label.StartsWith("commit"));
        Assert.Contains(pane.Transitions, t => t.From == hold && t.To == delib && t.Label == "after 10s");
        Assert.DoesNotContain(pane.Transitions, t => t.IsSelfLoop);
    }

    [Fact]
    public void Twins_Has_Two_Regions_And_A_Self_Loop()
    {
        var pane = new ControlPaneViewModel();
        pane.Project(Golden("twins.sc.json"));

        Assert.Equal(2, pane.Regions.Count);
        Assert.Equal(2, pane.States.Count);
        Assert.Single(pane.Transitions);
        Assert.True(pane.Transitions[0].IsSelfLoop);
        Assert.Equal("SIGNAL_A", pane.Transitions[0].From.StateId);
    }

    [Fact]
    public void Skinner_DELIBERATE_Has_Two_Commit_Transitions_To_HOLD()
    {
        var pane = new ControlPaneViewModel();
        pane.Project(Golden("skinner.sc.json"));

        Assert.Equal(3, pane.Transitions.Count);
        var fromDelib = pane.Transitions.Where(t => t.From.StateId == "DELIBERATE").ToList();
        Assert.Equal(2, fromDelib.Count);
        Assert.All(fromDelib, t => Assert.Equal("HOLD", t.To.StateId));
    }

    [Fact]
    public void Caravan_Has_Five_Regions_One_State_Each()
    {
        var pane = new ControlPaneViewModel();
        pane.Project(Golden("caravan.sc.json"));

        Assert.Equal(5, pane.Regions.Count);
        Assert.Equal(5, pane.States.Count);
        Assert.All(pane.Regions, r => Assert.Single(r.StateIds));
        Assert.Single(pane.Transitions);   // only S0 self-loops
        Assert.True(pane.Transitions[0].IsSelfLoop);
    }

    [Fact]
    public void Regions_Enclose_Their_States()
    {
        var pane = new ControlPaneViewModel();
        pane.Project(Golden("traffic_light.sc.json"));

        var region = pane.Regions[0];
        foreach (var sid in region.StateIds)
        {
            var s = pane.States.First(x => x.StateId == sid);
            Assert.True(s.X >= region.X, $"{sid}.X {s.X} < region.X {region.X}");
            Assert.True(s.Y >= region.Y, $"{sid}.Y {s.Y} < region.Y {region.Y}");
            Assert.True(s.X + ControlPaneViewModel.StateWidth <= region.X + region.Width + 0.01,
                $"{sid} overflows region width");
        }
    }

    [Fact]
    public void Every_Golden_Projects_With_Resolved_Transitions_And_An_Initial()
    {
        foreach (var file in CorpusLocator.GoldenChartFiles())
        {
            var name = Path.GetFileName(file);
            var pane = new ControlPaneViewModel();
            pane.Project(StatechartJson.Load(File.ReadAllText(file)));

            Assert.True(pane.Regions.Count > 0, $"{name}: no regions");
            Assert.True(pane.States.Count > 0, $"{name}: no states");
            foreach (var t in pane.Transitions)
            {
                Assert.Contains(t.From, pane.States);
                Assert.Contains(t.To, pane.States);
            }
            Assert.Contains(pane.States, s => s.IsInitial);
        }
    }
}
