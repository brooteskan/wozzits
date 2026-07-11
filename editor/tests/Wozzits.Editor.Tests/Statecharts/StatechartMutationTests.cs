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
    public void Add_Read_Op_Defaults_To_The_First_Agent()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);

        var node = pane.AddOp(OpKind.Marginal)!;

        var op = chart.Pure.First(p => p.Id == node.NodeId);
        Assert.Equal("sig", op.Agent);   // seeded with the first agent
        Assert.True(node.IsReadOp);
        Assert.Equal("sig", node.SelectedReadAgent);
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Add_Proximity_Op_Defaults_To_The_First_Binding()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);

        var node = pane.AddOp(OpKind.Proximity)!;

        var op = chart.Pure.First(p => p.Id == node.NodeId);
        Assert.Equal("lamp_0", op.Target);   // seeded with the first binding
        Assert.True(node.IsProximityOp);
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Change_A_Read_Ops_Agent_Rewires_It()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        var second = pane.AddAgent()!;                 // now sig + agent1
        var read = pane.AddOp(OpKind.Committed)!;      // defaults to sig

        var readVm = pane.Nodes.First(n => n.NodeId == read.NodeId);
        readVm.SelectedReadAgent = second.NodeId;      // the inspector picker

        Assert.Equal(second.NodeId, chart.Pure.First(p => p.Id == read.NodeId).Agent);
        Assert.Contains(pane.Wires, w => w.From.NodeId == second.NodeId && w.To.NodeId == read.NodeId);
        Assert.Empty(StatechartJson.Validate(chart));
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

    [Fact]
    public void Delete_A_Freshly_Added_Binding()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        var added = pane.AddBinding()!;   // selected after adding, unreferenced
        int before = chart.Bindings.Count;

        ((IEditorCanvas)pane).DeleteSelected();

        Assert.Equal(before - 1, chart.Bindings.Count);
        Assert.DoesNotContain(chart.Bindings, b => b.Port == added.NodeId);
        Assert.True(pane.IsDirty);
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Delete_A_Referenced_Binding_Drops_Its_Actuator_Effects()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        pane.SelectOnly(pane.Nodes.First(n => n.NodeId == "lamp_0"));   // driven by set_scale/set_visible

        ((IEditorCanvas)pane).DeleteSelected();

        Assert.DoesNotContain(chart.Bindings, b => b.Port == "lamp_0");
        Assert.DoesNotContain(chart.States.SelectMany(s => s.Do), e => e.TargetBind == "lamp_0");
        Assert.Empty(StatechartJson.Validate(chart));   // still valid after the cleanup
    }

    [Fact]
    public void Delete_An_Agent_Removes_Its_Reads_And_Writes()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        pane.SelectOnly(pane.Nodes.First(n => n.Kind == DataflowNodeKind.Agent));   // sig, the only agent

        ((IEditorCanvas)pane).DeleteSelected();

        Assert.Empty(chart.Agents);
        Assert.DoesNotContain(chart.Pure, p => p.IsRead);   // its reads went with it
        Assert.Empty(StatechartJson.Validate(chart));       // reverted commit triggers keep it valid
    }

    // ---- M2: wiring -------------------------------------------------------------

    [Fact]
    public void Connect_Wires_An_Op_Output_To_An_Operand()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        var a = pane.AddOp(OpKind.Mul)!;
        var b = pane.AddOp(OpKind.Add)!;

        bool ok = pane.TryConnect(a.NodeId, b.NodeId, 0);

        Assert.True(ok);
        var bOp = chart.Pure.First(p => p.Id == b.NodeId);
        Assert.Equal(RefKind.Op, bOp.Ins[0].Kind);
        Assert.Equal(a.NodeId, bOp.Ins[0].Op);
        Assert.Contains(pane.Wires, w => w.From.NodeId == a.NodeId && w.To.NodeId == b.NodeId);
        Assert.True(pane.IsDirty);
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Connect_Rejects_A_Cycle()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        var a = pane.AddOp(OpKind.Mul)!;
        var b = pane.AddOp(OpKind.Add)!;
        Assert.True(pane.TryConnect(a.NodeId, b.NodeId, 0));   // a -> b

        bool ok = pane.TryConnect(b.NodeId, a.NodeId, 0);      // b -> a would close a loop

        Assert.False(ok);
        Assert.Equal(RefKind.Const, chart.Pure.First(p => p.Id == a.NodeId).Ins[0].Kind);
    }

    [Fact]
    public void Connect_Rejects_A_Leaf_Source()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        var b = pane.AddOp(OpKind.Add)!;
        var agent = pane.Nodes.First(n => n.Kind == DataflowNodeKind.Agent);

        // An agent/binding can't feed an operand (operands are Const|op-ref); route via a read op.
        Assert.False(pane.TryConnect(agent.NodeId, b.NodeId, 0));
        Assert.Equal(RefKind.Const, chart.Pure.First(p => p.Id == b.NodeId).Ins[0].Kind);
    }

    [Fact]
    public void Connect_Rejects_A_Read_Op_Target()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        var a = pane.AddOp(OpKind.Add)!;
        var read = pane.Nodes.First(n => n.NodeId == "z");   // marginal read; its input is an agent

        Assert.False(pane.TryConnect(a.NodeId, read.NodeId, 0));
    }

    [Fact]
    public void Disconnect_Reverts_A_Wired_Operand_To_A_Constant()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        var a = pane.AddOp(OpKind.Mul)!;
        var b = pane.AddOp(OpKind.Add)!;
        pane.TryConnect(a.NodeId, b.NodeId, 0);
        Assert.Equal(RefKind.Op, chart.Pure.First(p => p.Id == b.NodeId).Ins[0].Kind);

        bool ok = pane.Disconnect(b.NodeId, 0);

        Assert.True(ok);
        Assert.Equal(RefKind.Const, chart.Pure.First(p => p.Id == b.NodeId).Ins[0].Kind);
        Assert.True(pane.IsDirty);
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Disconnect_A_Constant_Input_Is_A_Noop()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        var b = pane.AddOp(OpKind.Add)!;

        Assert.False(pane.Disconnect(b.NodeId, 0));   // in0 is already a constant
    }

    // ---- M3: transition draw ----------------------------------------------------

    [Fact]
    public void Add_Transition_Links_Two_States_With_An_Editable_After_Trigger()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);

        bool ok = pane.TryAddTransition("DELIBERATE", "HOLD");

        Assert.True(ok);
        var delib = chart.States.First(s => s.Id == "DELIBERATE");
        Assert.Contains(delib.Transitions, t => t.Target == "HOLD" && t.Trigger.Kind == TriggerKind.After);
        Assert.True(pane.IsDirty);
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Add_Self_Loop_Transition()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);

        Assert.True(pane.TryAddTransition("HOLD", "HOLD"));

        Assert.Contains(chart.States.First(s => s.Id == "HOLD").Transitions, t => t.Target == "HOLD");
        Assert.Contains(pane.Transitions, t => t.IsSelfLoop && t.From.StateId == "HOLD");
    }

    [Fact]
    public void Add_Transition_Rejects_An_Unknown_Target()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);

        Assert.False(pane.TryAddTransition("HOLD", "NOPE"));
        Assert.False(pane.IsDirty);
    }

    [Fact]
    public void Arming_And_Disarming_The_Transition_Tool_Flips_The_Flag()
    {
        var pane = Control("traffic_light.sc.json");
        Assert.False(pane.IsDrawingTransition);

        pane.IsDrawingTransition = true;
        Assert.True(pane.IsDrawingTransition);

        pane.DisarmTransitionDraw();
        Assert.False(pane.IsDrawingTransition);
    }

    [Fact]
    public void Parallel_Transitions_Get_Distinct_Lanes()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);

        // DELIBERATE already commits to HOLD; add a second DELIBERATE -> HOLD.
        pane.TryAddTransition("DELIBERATE", "HOLD");

        var parallels = pane.Transitions
            .Where(t => t.From.StateId == "DELIBERATE" && t.To.StateId == "HOLD")
            .Select(t => t.Lane)
            .OrderBy(lane => lane)
            .ToArray();

        Assert.Equal(new[] { 0, 1 }, parallels);   // fanned, not stacked on lane 0
    }

    [Fact]
    public void Delete_Transition_Removes_It_From_The_Owning_State()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);

        var hold = chart.States.First(s => s.Id == "HOLD");
        var doomed = hold.Transitions[0];
        int before = hold.Transitions.Count;

        pane.DeleteTransition(hold, doomed);

        Assert.Equal(before - 1, chart.States.First(s => s.Id == "HOLD").Transitions.Count);
        Assert.DoesNotContain(doomed, chart.States.First(s => s.Id == "HOLD").Transitions);
        Assert.True(pane.IsDirty);
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Transition_Row_Delete_Command_Removes_The_Transition()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);

        var holdVm = pane.States.First(s => s.StateId == "HOLD");
        int before = chart.States.First(s => s.Id == "HOLD").Transitions.Count;

        holdVm.TransitionRows[0].DeleteCommand.Execute(null);   // the inspector's Delete button

        Assert.Equal(before - 1, chart.States.First(s => s.Id == "HOLD").Transitions.Count);
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Change_Trigger_Kind_After_To_Commit_Defaults_The_Agent()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);

        var hold = chart.States.First(s => s.Id == "HOLD");
        var t = hold.Transitions.First(x => x.Trigger.Kind == TriggerKind.After);

        pane.SetTriggerKind(hold, t, TriggerKind.Commit);

        Assert.Equal(TriggerKind.Commit, t.Trigger.Kind);
        Assert.Equal("sig", t.Trigger.Agent);   // the chart's only agent
        Assert.Null(t.Trigger.Outcome);          // fires on any outcome
        Assert.True(pane.IsDirty);
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Change_Trigger_Kind_Commit_To_After_Defaults_Seconds()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);

        var delib = chart.States.First(s => s.Id == "DELIBERATE");
        var t = delib.Transitions.First(x => x.Trigger.Kind == TriggerKind.Commit);

        pane.SetTriggerKind(delib, t, TriggerKind.After);

        Assert.Equal(TriggerKind.After, t.Trigger.Kind);
        Assert.True(t.Trigger.Seconds > 0);   // defaulted to 1s
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Transition_Row_Kind_Picker_Changes_The_Trigger_And_Arrow_Label()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);

        var holdVm = pane.States.First(s => s.StateId == "HOLD");
        holdVm.TransitionRows[0].SelectedTriggerKind = TriggerKind.Commit;   // the inspector picker

        Assert.Contains(chart.States.First(s => s.Id == "HOLD").Transitions, x => x.Trigger.Kind == TriggerKind.Commit);
        Assert.Contains(pane.Transitions, tr => tr.From.StateId == "HOLD" && tr.Label == "commit");
        Assert.True(pane.IsDirty);
    }

    // ---- M4: add / remove effects -----------------------------------------------

    [Fact]
    public void Add_Do_Effect_Appends_With_Defaults()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);
        var delib = chart.States.First(s => s.Id == "DELIBERATE");
        int before = delib.Do.Count;

        pane.AddEffect(delib, EffectSlot.Do, EffectKind.SetGoal);

        Assert.Equal(before + 1, delib.Do.Count);
        var added = delib.Do.Last();
        Assert.Equal(EffectKind.SetGoal, added.Kind);
        Assert.Equal("sig", added.Agent);   // seeded with the first agent
        Assert.NotNull(added.Value);         // an editable constant
        Assert.True(pane.IsDirty);
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Add_Actuator_Effect_Targets_The_First_Binding()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);
        var hold = chart.States.First(s => s.Id == "HOLD");

        pane.AddEffect(hold, EffectSlot.Entry, EffectKind.SetScale);

        Assert.Equal("lamp_0", hold.Entry.Last().TargetBind);   // seeded with the first binding
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Delete_Effect_Removes_It_From_Its_List()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);
        var delib = chart.States.First(s => s.Id == "DELIBERATE");
        var doomed = delib.Do[0];
        int before = delib.Do.Count;

        pane.DeleteEffect(delib, doomed);

        Assert.Equal(before - 1, delib.Do.Count);
        Assert.DoesNotContain(doomed, delib.Do);
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Add_Effect_Command_Appends_To_The_Do_List()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);
        var delibVm = pane.States.First(s => s.StateId == "DELIBERATE");
        int before = chart.States.First(s => s.Id == "DELIBERATE").Do.Count;

        delibVm.AddDoEffectCommand.Execute(EffectKind.Rearm);   // the "+ do" menu

        Assert.Equal(before + 1, chart.States.First(s => s.Id == "DELIBERATE").Do.Count);
        Assert.Contains(chart.States.First(s => s.Id == "DELIBERATE").Do, e => e.Kind == EffectKind.Rearm);
    }

    [Fact]
    public void Effect_Row_Delete_Command_Removes_The_Effect()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);
        var delibVm = pane.States.First(s => s.StateId == "DELIBERATE");
        int before = chart.States.First(s => s.Id == "DELIBERATE").Do.Count;

        delibVm.DoEffectRows[0].DeleteCommand.Execute(null);   // the inspector's row delete

        Assert.Equal(before - 1, chart.States.First(s => s.Id == "DELIBERATE").Do.Count);
        Assert.True(pane.IsDirty);
    }

    // ---- M4: add agent / binding, set-initial (authoring from scratch) ----------

    [Fact]
    public void Add_Agent_Appends_An_Owned_Agent_With_An_Editable_Spec()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        int before = chart.Agents.Count;

        var node = pane.AddAgent();

        Assert.NotNull(node);
        Assert.Equal(before + 1, chart.Agents.Count);
        var added = chart.Agents.First(a => a.Id == node!.NodeId);
        Assert.True(added.Owned);
        Assert.Equal("self", added.Host);
        Assert.NotNull(added.Spec);
        Assert.True(node!.HasSpecFields);       // its numeric spec is editable
        Assert.Same(node, pane.SelectedNode);
        Assert.True(pane.IsDirty);
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Add_Binding_Appends_A_Binding_Whose_Find_Is_Editable()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = Dataflow(chart);
        int before = chart.Bindings.Count;

        var node = pane.AddBinding()!;
        pane.ClearDirty();

        Assert.Equal(before + 1, chart.Bindings.Count);
        Assert.True(node.HasBindingFields);

        node.BindingFields[0].Value = "cube_7";   // point it at an entity

        Assert.Equal("cube_7", chart.Bindings.First(b => b.Port == node.NodeId).Find);
        Assert.True(pane.IsDirty);                // the edit re-marked the chart
        Assert.Empty(StatechartJson.Validate(chart));
    }

    [Fact]
    public void Set_Initial_Repoints_The_Region()
    {
        var chart = Golden("traffic_light.sc.json");
        var pane = new ControlPaneViewModel();
        pane.Project(chart);
        var hold = chart.States.First(s => s.Id == "HOLD");   // DELIBERATE is the initial

        pane.SetInitial(hold);

        Assert.Equal("HOLD", chart.Regions.First(r => r.States.Contains("HOLD")).Initial);
        Assert.True(pane.States.First(s => s.StateId == "HOLD").IsInitial);
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Author_A_Well_Formed_Chart_From_An_Empty_One()
    {
        var chart = new Chart { Name = "scratch" };
        var control = new ControlPaneViewModel();
        var dataflow = new DataflowPaneViewModel();
        control.Project(chart);
        dataflow.Project(chart);

        var agent = dataflow.AddAgent()!;
        dataflow.AddBinding();
        var state = control.AddState()!;                                  // becomes its region's initial
        control.AddEffect(state.Model, EffectSlot.Do, EffectKind.SetGoal); // drives the new agent

        Assert.Single(chart.Agents);
        Assert.Single(chart.Bindings);
        Assert.Single(chart.States);
        Assert.NotEmpty(chart.States[0].Do);
        Assert.Equal(agent.NodeId, chart.States[0].Do[0].Agent);   // effect targets the added agent
        Assert.Empty(StatechartJson.Validate(chart));               // the hand-built chart is valid
    }

    [Fact]
    public void Author_The_Full_Loop_A_Read_Feeds_An_Actuator()
    {
        var chart = new Chart { Name = "loop" };
        var control = new ControlPaneViewModel();
        var dataflow = new DataflowPaneViewModel();
        control.Project(chart);
        dataflow.Project(chart);

        dataflow.AddAgent();                                  // the brain
        dataflow.AddBinding();                                // the cube
        var read = dataflow.AddOp(OpKind.Marginal)!;          // read its live leaning
        var state = control.AddState()!;
        control.AddEffect(state.Model, EffectSlot.Do, EffectKind.SetScale);   // scale the cube

        // source that effect's value from the read op — the terminal link (inspector picker)
        var effectRow = control.States.First(s => s.StateId == state.StateId).DoEffectRows[0];
        effectRow.SelectedValueSource = read.NodeId;

        var value = chart.States[0].Do[0].Value!;
        Assert.Equal(RefKind.Op, value.Kind);
        Assert.Equal(read.NodeId, value.Op);             // scale follows the agent's reading
        Assert.Empty(StatechartJson.Validate(chart));    // agent → read → actuator, well-formed
    }
}
