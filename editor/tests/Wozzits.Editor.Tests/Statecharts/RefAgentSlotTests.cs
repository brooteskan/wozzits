using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

// A read op on a REFERENCED agent (owned:false) can't know the slot count from the chart
// alone -- the slots live on the scene node the chart runs on. The pane takes a host-supplied
// resolver: with it the slot picker bounds to 0..N-1; a double-click routes the ref to the
// mind-open resolver. (A ref is exactly the tank_combat "mind" case.)
public sealed class RefAgentSlotTests
{
    private static Chart RefReadingChart()
    {
        var chart = new Chart { Name = "t" };
        chart.Agents.Add(new AgentDecl { Id = "mind", Owned = false, Host = "self" });
        chart.Pure.Add(new PureOp
        {
            Id = "posture_c",
            Op = OpKind.Committed,
            Agent = "mind",
            Slot = 1,
        });
        return chart;
    }

    [Fact]
    public void RefRead_Has_No_Slot_Bound_Without_A_Resolver()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(RefReadingChart());

        var read = pane.Nodes.First(n => n.NodeId == "posture_c");
        Assert.Equal(0, read.SlotChoiceCount);   // ref unresolved -> free-text slot field
        Assert.False(read.HasSlotChoices);
    }

    [Fact]
    public void Resolver_Bounds_The_RefRead_Slot_Picker()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(RefReadingChart());

        // The host window resolves the count from the scene (here 5, e.g. the tank's
        // decisions:5). Setting the resolver reprojects, turning the free-text field into a
        // 0..4 dropdown.
        pane.SetRefAgentResolvers(_ => 5, _ => { });

        var read = pane.Nodes.First(n => n.NodeId == "posture_c");
        Assert.Equal(5, read.SlotChoiceCount);
        Assert.True(read.HasSlotChoices);
        Assert.Equal(5, read.SlotChoices.Count);   // a 0..4 dropdown
    }

    [Fact]
    public void DoubleClick_On_A_Ref_Agent_Routes_To_The_Mind_Open_Resolver()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(RefReadingChart());

        AgentDecl? opened = null;
        pane.SetRefAgentResolvers(_ => 5, a => opened = a);

        pane.ActivateNode(pane.Nodes.First(n => n.NodeId == "mind"));   // the ref agent card
        Assert.NotNull(opened);
        Assert.Equal("mind", opened!.Id);

        // A non-ref card (the read op) does not trigger the open.
        opened = null;
        pane.ActivateNode(pane.Nodes.First(n => n.NodeId == "posture_c"));
        Assert.Null(opened);
    }
}
