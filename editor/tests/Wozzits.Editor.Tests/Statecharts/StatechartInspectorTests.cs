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
    public void Binding_Node_Inspector_Shows_Editable_Find_And_Scope()
    {
        var player = Dataflow("zeno.sc.json").Nodes.First(n => n.NodeId == "player");   // find "tank" global

        Assert.Contains(player.BindingFields, f => f.Name == "find" && f.Value == "tank");   // now editable
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
    public void State_Effect_Rows_Expose_Editable_Constants_And_Op_Markers()
    {
        var s0 = Control("caravan.sc.json").States.First(s => s.StateId == "S0");

        // set_goal = 0.8 is a constant -> editable; set_scale = op:... is not.
        var goal = s0.DoEffectRows.First(r => r.Label.StartsWith("set_goal"));
        Assert.True(goal.IsEditable);
        Assert.Equal("0.8", goal.ValueEditor!.Value);

        var scale = s0.DoEffectRows.First(r => r.Label.StartsWith("set_scale"));
        Assert.False(scale.IsEditable);
        Assert.True(scale.HasReadOnlyValue);
    }

    [Fact]
    public void Transition_Rows_Expose_Editable_After_Delay()
    {
        var control = Control("traffic_light.sc.json");

        var hold = control.States.First(s => s.StateId == "HOLD");
        var after = hold.TransitionRows.First(r => r.IsAfter);
        Assert.Equal("10", after.SecondsEditor!.Value);
        Assert.True(after.HasActions);   // rearm sig

        // A commit-triggered transition is not editable-as-delay; it reads as text.
        var delib = control.States.First(s => s.StateId == "DELIBERATE");
        var commit = delib.TransitionRows.First();
        Assert.False(commit.IsAfter);
        Assert.True(commit.HasTriggerText);
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
