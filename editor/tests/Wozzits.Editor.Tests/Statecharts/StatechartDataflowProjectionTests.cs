using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>E2a: the dataflow-layer projection (Chart -> node/wire view-models + layout).</summary>
public sealed class StatechartDataflowProjectionTests
{
    private static Chart Golden(string file) =>
        StatechartJson.Load(File.ReadAllText(Path.Combine(CorpusLocator.StatechartsDir(), file)));

    private static bool HasWire(DataflowPaneViewModel pane, string fromId, string toId) =>
        pane.Wires.Any(w => w.From.NodeId == fromId && w.To.NodeId == toId);

    [Fact]
    public void TrafficLight_Projects_A_Node_Per_Binding_Agent_And_Op()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(Golden("traffic_light.sc.json"));

        Assert.Equal(2, pane.Nodes.Count(n => n.Kind == DataflowNodeKind.Binding));
        Assert.Equal(1, pane.Nodes.Count(n => n.Kind == DataflowNodeKind.Agent));
        Assert.Equal(12, pane.Nodes.Count(n => n.Kind == DataflowNodeKind.Op));
        Assert.Equal(15, pane.Nodes.Count);
        Assert.True(pane.HasGraph);
    }

    [Fact]
    public void TrafficLight_Wires_Agent_Reads_And_Op_Dependencies()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(Golden("traffic_light.sc.json"));

        // 12 wired inputs: agent->z, agent->c, z->md0, z->md1, md0->p0, md1->p1,
        // p0->s0d, p1->s1d, c->is0, c->is1, is0->s0h, is1->s1h. (consts are not wires)
        Assert.Equal(12, pane.Wires.Count);
        Assert.True(HasWire(pane, "sig", "z"));
        Assert.True(HasWire(pane, "sig", "c"));
        Assert.True(HasWire(pane, "z", "md0"));
        Assert.True(HasWire(pane, "c", "is0"));
        Assert.True(HasWire(pane, "is0", "s0h"));
    }

    [Fact]
    public void Const_Operands_Are_Inline_Literals_Not_Wires()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(Golden("traffic_light.sc.json"));

        var md0 = pane.Nodes.First(n => n.NodeId == "md0");   // mul_add(z, 0.5, 0.5)
        Assert.Equal(3, md0.InputPorts.Count);
        Assert.Equal(1, md0.InputPorts.Count(p => p.IsWired));
        Assert.Equal(2, md0.InputPorts.Count(p => p.IsConstant));
        Assert.Contains(md0.InputPorts, p => p.IsConstant && p.ConstantText == "0.5");
    }

    [Fact]
    public void Layout_Runs_Left_To_Right_Along_Every_Wire()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(Golden("traffic_light.sc.json"));

        Assert.NotEmpty(pane.Wires);
        foreach (var w in pane.Wires)
        {
            Assert.True(w.From.X < w.To.X, $"wire {w.From.NodeId}->{w.To.NodeId} is not left-to-right");
            Assert.True(w.From.Column < w.To.Column, $"wire {w.From.NodeId}->{w.To.NodeId} column not increasing");
        }
    }

    [Fact]
    public void Zeno_Wires_Proximity_From_Its_Player_Binding()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(Golden("zeno.sc.json"));

        Assert.True(HasWire(pane, "player", "dist"));   // binding -> proximity target
    }

    [Fact]
    public void Reproject_Replaces_The_Previous_Graph()
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(Golden("caravan.sc.json"));
        var firstCount = pane.Nodes.Count;
        Assert.True(firstCount > 0);

        pane.Project(Golden("traffic_light.sc.json"));
        Assert.Equal(15, pane.Nodes.Count);
        Assert.NotEqual(firstCount, pane.Nodes.Count);
    }

    [Fact]
    public void Every_Golden_Projects_With_Only_Resolved_Wires()
    {
        foreach (var file in Directory.GetFiles(CorpusLocator.StatechartsDir(), "*.sc.json"))
        {
            var pane = new DataflowPaneViewModel();
            pane.Project(StatechartJson.Load(File.ReadAllText(file)));

            var name = Path.GetFileName(file);
            Assert.True(pane.Nodes.Count > 0, $"{name}: projected no nodes");
            foreach (var w in pane.Wires)
            {
                Assert.Contains(w.From, pane.Nodes);
                Assert.Contains(w.To, pane.Nodes);
            }
        }
    }
}
