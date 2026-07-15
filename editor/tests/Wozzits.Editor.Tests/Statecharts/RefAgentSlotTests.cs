using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

// A chart's REF agent (owned:false) can NAME a mind (.mind.json) directly. The editor uses
// that name to bound a read op's slot picker (0..N-1 from the mind's qubits) and to open the
// mind on double-click -- with NO scene node in the loop. (This is the tank_combat "mind"
// case done right: the reference lives in the chart, so opening the chart can open the mind.)
public sealed class RefAgentSlotTests
{
    private static Chart RefReadingChart(string mind = "")
    {
        var chart = new Chart { Name = "t" };
        chart.Agents.Add(new AgentDecl { Id = "mind", Owned = false, Host = "self", Mind = mind });
        chart.Pure.Add(new PureOp { Id = "posture_c", Op = OpKind.Committed, Agent = "mind", Slot = 1 });
        return chart;
    }

    // The pane's real resolver reads the ref's Mind and loads that .mind.json's qubit count.
    // Here we stub it: "tank_mind" -> 5 qubits, anything else -> unknown (0).
    private static int SlotsForMind(AgentDecl a) => a.Mind == "tank_mind" ? 5 : 0;

    [Fact]
    public void RefRead_Has_No_Slot_Bound_Until_A_Mind_Is_Named()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(RefReadingChart());
        pane.SetRefAgentResolvers(SlotsForMind, _ => { });

        var read = pane.Nodes.First(n => n.NodeId == "posture_c");
        Assert.Equal(0, read.SlotChoiceCount);   // no mind named -> free-text slot field
        Assert.False(read.HasSlotChoices);
    }

    [Fact]
    public void Ref_Naming_A_Mind_Bounds_The_Slot_Picker()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(RefReadingChart("tank_mind"));
        pane.SetRefAgentResolvers(SlotsForMind, _ => { });

        var read = pane.Nodes.First(n => n.NodeId == "posture_c");
        Assert.Equal(5, read.SlotChoiceCount);   // 0..4 from tank_mind's qubits
        Assert.True(read.HasSlotChoices);
    }

    [Fact]
    public void Selecting_A_Mind_Sets_The_Ref_And_Rebounds_The_Slots()
    {
        var chart = RefReadingChart();
        var pane = new DataflowPaneViewModel();
        pane.Project(chart);
        pane.SetRefAgentResolvers(SlotsForMind, _ => { });

        pane.Nodes.First(n => n.NodeId == "mind").SelectedMind = "tank_mind";

        Assert.Equal("tank_mind", chart.Agents[0].Mind);
        // Choosing the mind reprojects, so the read op's picker now bounds to 0..4.
        Assert.Equal(5, pane.Nodes.First(n => n.NodeId == "posture_c").SlotChoiceCount);
    }

    [Fact]
    public void DoubleClick_On_A_Ref_Routes_The_Named_Mind_To_The_Open_Resolver()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(RefReadingChart("tank_mind"));

        AgentDecl? opened = null;
        pane.SetRefAgentResolvers(SlotsForMind, a => opened = a);

        pane.ActivateNode(pane.Nodes.First(n => n.NodeId == "mind"));   // the ref agent card
        Assert.Equal("tank_mind", opened?.Mind);

        // A non-ref card (the read op) does not trigger the open.
        opened = null;
        pane.ActivateNode(pane.Nodes.First(n => n.NodeId == "posture_c"));
        Assert.Null(opened);
    }

    // Regression: setting the mind must NOT reproject (which would rebuild the very node the
    // ComboBox is bound to and feed back into the selection) -- exactly one ref node before and
    // after, and the chosen value sticks. (The reported bug: a duplicate node, both showing
    // "(none)", and deleting one removing both.)
    [Fact]
    public void Setting_A_Mind_Does_Not_Duplicate_The_Ref_Node_Or_Lose_The_Value()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(RefReadingChart());
        pane.SetRefAgentResolvers(SlotsForMind, _ => { });

        Assert.Single(pane.Nodes.Where(n => n.NodeId == "mind"));
        pane.Nodes.First(n => n.NodeId == "mind").SelectedMind = "tank_mind";

        Assert.Single(pane.Nodes.Where(n => n.NodeId == "mind"));   // no duplicate
        Assert.Equal("tank_mind", pane.Nodes.First(n => n.NodeId == "mind").SelectedMind);   // value stuck
    }

    [Fact]
    public void Ref_Mind_RoundTrips_Through_Json()
    {
        var ir = StatechartJson.Emit(RefReadingChart("tank_mind"), indented: false);
        var back = StatechartJson.Load(ir);
        Assert.Equal("tank_mind", back.Agents.First(a => a.Id == "mind").Mind);
    }
}
