using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>Phase-2 inspector: the read-only detail + single-selection tracking behind the panels.</summary>
public sealed class StatechartInspectorTests
{
    private static Chart Golden(string file) =>
        StatechartJson.Load(File.ReadAllText(Path.Combine(CorpusLocator.StatechartsDir(), file)));

    private static DataflowPaneViewModel Dataflow(string file)
    {
        var pane = new DataflowPaneViewModel();
        pane.Project(Golden(file));
        return pane;
    }

    private static ControlPaneViewModel Control(string file)
    {
        var pane = new ControlPaneViewModel();
        pane.Project(Golden(file));
        return pane;
    }

    [Fact]
    public void Agent_Node_Inspector_Shows_Owned_Host_And_Spec_Fields()
    {
        var agent = Dataflow("traffic_light.sc.json").Nodes.First(n => n.Kind == DataflowNodeKind.Agent);

        Assert.Contains(agent.PropertyRows, r => r.Name == "owned" && r.Value == "yes");
        Assert.Contains(agent.PropertyRows, r => r.Name == "host" && r.Value == "self");
        Assert.Contains(agent.SpecFields, f => f.Name == "decisions");   // spec is now an editable field
    }

    [Fact]
    public void Read_Op_Inspector_Shows_Agent_And_Slot()
    {
        var z = Dataflow("traffic_light.sc.json").Nodes.First(n => n.NodeId == "z");   // marginal

        Assert.Contains(z.PropertyRows, r => r.Name == "agent" && r.Value == "sig");
        Assert.Contains(z.PropertyRows, r => r.Name == "slot" && r.Value == "0");
    }

    [Fact]
    public void Binding_Node_Inspector_Shows_Find_And_Scope()
    {
        var player = Dataflow("zeno.sc.json").Nodes.First(n => n.NodeId == "player");   // find "tank" global

        Assert.Contains(player.PropertyRows, r => r.Name == "find" && r.Value == "tank");
        Assert.Contains(player.PropertyRows, r => r.Name == "scope" && r.Value == "global");
    }

    [Fact]
    public void SelectedNode_Tracks_Only_A_Single_Selection()
    {
        var pane = Dataflow("traffic_light.sc.json");
        var canvas = (IEditorCanvas)pane;
        Assert.Null(pane.SelectedNode);

        canvas.SelectOnly(pane.Nodes[0]);
        Assert.Same(pane.Nodes[0], pane.SelectedNode);
        Assert.True(pane.HasSelectedNode);

        canvas.ToggleSelection(pane.Nodes[1]);   // two selected -> no single detail
        Assert.Null(pane.SelectedNode);

        canvas.ClearSelection();
        Assert.Null(pane.SelectedNode);
        Assert.False(pane.HasSelectedNode);
    }

    [Fact]
    public void State_Inspector_Lists_Effects_And_Transitions()
    {
        var delib = Control("traffic_light.sc.json").States.First(s => s.StateId == "DELIBERATE");

        Assert.NotEmpty(delib.DoEffects);
        Assert.Contains(delib.DoEffects, e => e.StartsWith("set_scale"));
        Assert.Contains(delib.OutgoingTransitions, t => t.Contains("HOLD") && t.Contains("commit"));
    }

    [Fact]
    public void State_Inspector_Formats_Goal_Effects()
    {
        var delib = Control("skinner.sc.json").States.First(s => s.StateId == "DELIBERATE");
        Assert.Contains(delib.DoEffects, e => e.StartsWith("set_goal"));
    }

    [Fact]
    public void SelectedState_Tracks_Single_Selection()
    {
        var pane = Control("traffic_light.sc.json");
        var canvas = (IEditorCanvas)pane;
        Assert.Null(pane.SelectedState);

        canvas.SelectOnly(pane.States[0]);
        Assert.Same(pane.States[0], pane.SelectedState);
        Assert.True(pane.HasSelectedState);
    }
}
