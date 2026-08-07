using System.Text.Json.Nodes;
using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>The combined chart document: shared chart, cross-layer focus, and save (E-phase 4).</summary>
public sealed class StatechartDocumentTests
{
    private static Chart Golden(string file) =>
        StatechartJson.Load(File.ReadAllText(Path.Combine(CorpusLocator.StatechartsDir(), file)));

    private static string FreshChartPath(string fileName)
    {
        var dir = Path.Combine(Path.GetTempPath(), "wz-sc-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        return Path.Combine(dir, fileName);
    }

    private static StatechartDocumentViewModel Open(string file) =>
        new("traffic_light", FreshChartPath(file), Golden(file));

    [Fact]
    public void CompiledIr_Is_The_Minified_Chart_And_Tracks_Edits()
    {
        // The IR the save-time runner refresh re-embeds: minified, as authored, and live.
        var chart = Golden("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", FreshChartPath("cir.sc.json"), chart);

        var ir = document.CompiledIr;
        Assert.DoesNotContain("\n", ir);              // minified (chart_ir), not the indented .sc.json
        Assert.Contains("\"find\":\"lamp_0\"", ir);   // as authored

        chart.Bindings.First(b => b.Port == "lamp_0").Find = "reactor";   // edit the shared chart
        Assert.Contains("\"find\":\"reactor\"", document.CompiledIr);     // re-embed reflects it
    }

    [Fact]
    public void Document_Projects_Both_Canvases_From_One_Chart()
    {
        var document = Open("traffic_light.sc.json");

        Assert.True(document.Control.HasGraph);
        Assert.True(document.Dataflow.HasGraph);
        Assert.Equal(2, document.Control.States.Count);
        Assert.Equal(15, document.Dataflow.Nodes.Count);
    }

    [Fact]
    public void Selecting_A_State_Dims_The_Dataflow_That_Does_Not_Feed_It()
    {
        var document = Open("traffic_light.sc.json");

        ((IEditorCanvas)document.Control).SelectOnly(document.Control.States.First(s => s.StateId == "DELIBERATE"));

        Assert.False(document.Dataflow.Nodes.First(n => n.NodeId == "z").IsDimmed);
        Assert.False(document.Dataflow.Nodes.First(n => n.NodeId == "sig").IsDimmed);
        Assert.True(document.Dataflow.Nodes.First(n => n.NodeId == "s0h").IsDimmed);
        Assert.True(document.Dataflow.Nodes.First(n => n.NodeId == "c").IsDimmed);
    }

    [Fact]
    public void Deselecting_Restores_The_Full_Dataflow()
    {
        var document = Open("traffic_light.sc.json");
        var canvas = (IEditorCanvas)document.Control;

        canvas.SelectOnly(document.Control.States.First(s => s.StateId == "DELIBERATE"));
        Assert.Contains(document.Dataflow.Nodes, n => n.IsDimmed);

        canvas.ClearSelection();
        Assert.DoesNotContain(document.Dataflow.Nodes, n => n.IsDimmed);
    }

    [Fact]
    public void Layout_Round_Trips_Through_Json()
    {
        var layout = new StatechartLayout { ControlZoom = 1.5, DataflowZoom = 0.75 };
        layout.StatePositions["S0"] = new StatechartLayout.Point(10, 20);
        layout.NodePositions["z"] = new StatechartLayout.Point(30, 40);

        var back = StatechartLayout.FromJson(layout.ToJson());

        Assert.Equal(1.5, back.ControlZoom);
        Assert.Equal(0.75, back.DataflowZoom);
        Assert.Equal(new StatechartLayout.Point(10, 20), back.StatePositions["S0"]);
        Assert.Equal(new StatechartLayout.Point(30, 40), back.NodePositions["z"]);
    }

    [Fact]
    public void Save_Writes_The_Edited_Chart_And_A_Layout_Sidecar()
    {
        var path = FreshChartPath("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));
        var canvas = (IEditorCanvas)document.Control;

        canvas.SelectOnly(document.Control.States.First(s => s.StateId == "HOLD"));
        canvas.DeleteSelected();
        Assert.True(document.IsDirty);

        document.Save();

        var reloaded = StatechartJson.Load(File.ReadAllText(path));
        Assert.Single(reloaded.States);
        Assert.DoesNotContain(reloaded.States, s => s.Id == "HOLD");
        Assert.True(File.Exists(Path.ChangeExtension(path, ".editor.json")));
        Assert.False(document.IsDirty);   // cleared after save
    }

    [Fact]
    public void Unedited_Chart_Is_Not_Rewritten_On_Save()
    {
        // A move is layout-dirty but not chart-dirty, so Save writes the sidecar and leaves
        // the .sc.json untouched (no reformatting of a chart the user did not edit).
        var path = FreshChartPath("traffic_light.sc.json");
        File.WriteAllText(path, "SENTINEL");   // the doc has its chart in memory, not from here
        var document = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));

        ((IEditorCanvas)document.Control).SelectOnly(document.Control.States[0]);
        ((IEditorCanvas)document.Control).MoveSelectedBy(40, 40);
        document.Save();

        Assert.Equal("SENTINEL", File.ReadAllText(path));   // .sc.json left alone
        Assert.True(File.Exists(Path.ChangeExtension(path, ".editor.json")));
    }

    [Fact]
    public void Editing_An_Agent_Spec_Field_Marks_Dirty_And_Saves_Into_The_Chart()
    {
        var path = FreshChartPath("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));

        var agent = document.Dataflow.Nodes.First(n => n.Kind == DataflowNodeKind.Agent);
        agent.SpecFields.First(f => f.Name == "goal").Value = "0.9";

        Assert.True(document.Dataflow.IsDirty);
        Assert.True(document.IsDirty);

        document.Save();

        var reloaded = StatechartJson.Load(File.ReadAllText(path));
        var spec = (JsonObject)reloaded.Agents.First(a => a.Id == "sig").Spec!;
        Assert.Equal(0.9, spec["goal"]!.GetValue<double>());
    }

    [Fact]
    public void Editing_An_Op_Constant_Marks_Dirty_And_Saves_Into_The_Chart()
    {
        var path = FreshChartPath("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));

        // s0d = mul(p0, 3) -- the second input is the constant 3
        var mul = document.Dataflow.Nodes.First(n => n.NodeId == "s0d");
        mul.InputPorts.First(p => p.IsConstant).ConstEditor!.Value = "5";

        Assert.True(document.Dataflow.IsDirty);

        document.Save();

        var reloaded = StatechartJson.Load(File.ReadAllText(path));
        var s0d = reloaded.Pure.First(p => p.Id == "s0d");
        Assert.Contains(s0d.Ins, r => r.Kind == RefKind.Const && r.Const == 5);
    }

    [Fact]
    public void Editing_An_Effect_Constant_Marks_Dirty_And_Saves_Into_The_Chart()
    {
        var path = FreshChartPath("caravan.sc.json");
        var document = new StatechartDocumentViewModel("caravan", path, Golden("caravan.sc.json"));

        // S0's do has `set_goal caravan[0] = 0.8` -- an editable numeric constant.
        var s0 = document.Control.States.First(s => s.StateId == "S0");
        s0.DoEffectRows.First(r => r.IsEditable).ValueEditor!.Value = "0.5";

        Assert.True(document.Control.IsDirty);
        Assert.True(document.IsDirty);

        document.Save();

        var reloaded = StatechartJson.Load(File.ReadAllText(path));
        var goal = reloaded.States.First(s => s.Id == "S0").Do.First(e => e.Kind == EffectKind.SetGoal);
        Assert.Equal(0.5, goal.Value!.Const);
    }

    [Fact]
    public void Editing_A_Transition_After_Delay_Marks_Dirty_And_Saves_Into_The_Chart()
    {
        var path = FreshChartPath("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));

        // HOLD -> DELIBERATE fires on `after 10s` -- the delay is the editable scalar.
        var hold = document.Control.States.First(s => s.StateId == "HOLD");
        var after = hold.TransitionRows.First(r => r.IsAfter);
        after.SecondsEditor!.Value = "12";

        Assert.True(document.Control.IsDirty);
        Assert.True(document.IsDirty);

        document.Save();

        var reloaded = StatechartJson.Load(File.ReadAllText(path));
        var trigger = reloaded.States.First(s => s.Id == "HOLD").Transitions
            .First(t => t.Trigger.Kind == TriggerKind.After).Trigger;
        Assert.Equal(12, trigger.Seconds);
    }

    [Fact]
    public void Adding_An_Op_Marks_Dirty_And_Saves_Into_The_Chart()
    {
        var path = FreshChartPath("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));

        var node = document.Dataflow.AddOp(OpKind.Add);
        Assert.NotNull(node);
        Assert.True(document.IsDirty);

        document.Save();

        var reloaded = StatechartJson.Load(File.ReadAllText(path));
        Assert.Contains(reloaded.Pure, p => p.Id == node!.NodeId && p.Op == OpKind.Add);
    }

    [Fact]
    public void Wiring_An_Operand_Marks_Dirty_And_Saves_Into_The_Chart()
    {
        var path = FreshChartPath("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));

        var a = document.Dataflow.AddOp(OpKind.Mul)!;
        var b = document.Dataflow.AddOp(OpKind.Add)!;
        Assert.True(document.Dataflow.TryConnect(a.NodeId, b.NodeId, 0));
        Assert.True(document.IsDirty);

        document.Save();

        var reloaded = StatechartJson.Load(File.ReadAllText(path));
        var bOp = reloaded.Pure.First(p => p.Id == b.NodeId);
        Assert.Equal(RefKind.Op, bOp.Ins[0].Kind);
        Assert.Equal(a.NodeId, bOp.Ins[0].Op);
    }

    [Fact]
    public void Drawing_A_Transition_Marks_Dirty_And_Saves_Into_The_Chart()
    {
        var path = FreshChartPath("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));

        Assert.True(document.Control.TryAddTransition("HOLD", "HOLD"));   // a self-loop
        Assert.True(document.IsDirty);

        document.Save();

        var reloaded = StatechartJson.Load(File.ReadAllText(path));
        Assert.Contains(reloaded.States.First(s => s.Id == "HOLD").Transitions, t => t.Target == "HOLD");
    }

    [Fact]
    public void Adding_An_Effect_Marks_Dirty_And_Saves_Into_The_Chart()
    {
        var path = FreshChartPath("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));

        var delib = document.Control.States.First(s => s.StateId == "DELIBERATE");
        delib.AddDoEffectCommand.Execute(EffectKind.Rearm);
        Assert.True(document.IsDirty);

        document.Save();

        var reloaded = StatechartJson.Load(File.ReadAllText(path));
        Assert.Contains(reloaded.States.First(s => s.Id == "DELIBERATE").Do, e => e.Kind == EffectKind.Rearm);
    }

    [Fact]
    public void Layout_Sidecar_Persists_The_Read_Only_Flag()
    {
        Assert.True(StatechartLayout.FromJson(new StatechartLayout { ReadOnly = true }.ToJson()).ReadOnly);
        Assert.False(StatechartLayout.FromJson(new StatechartLayout().ToJson()).ReadOnly);
    }

    [Fact]
    public void A_Read_Only_Chart_Is_Not_Rewritten_But_The_Flag_Persists()
    {
        var path = FreshChartPath("traffic_light.sc.json");
        File.WriteAllText(path, "SENTINEL");   // the doc holds its chart in memory, not from here
        var document = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));

        document.IsReadOnly = true;
        ((IEditorCanvas)document.Control).SelectOnly(document.Control.States.First(s => s.StateId == "HOLD"));
        ((IEditorCanvas)document.Control).DeleteSelected();   // a real edit that must NOT be written
        Assert.True(document.IsDirty);

        document.Save();

        Assert.Equal("SENTINEL", File.ReadAllText(path));   // .sc.json protected
        var sidecar = StatechartLayout.FromJson(File.ReadAllText(Path.ChangeExtension(path, ".editor.json")));
        Assert.True(sidecar.ReadOnly);

        var reopened = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));
        Assert.True(reopened.IsReadOnly);   // restored from the sidecar
    }

    [Fact]
    public void Rename_Moves_The_Chart_File_And_Updates_The_Name()
    {
        var path = FreshChartPath("orig.sc.json");
        File.WriteAllText(path, StatechartJson.Emit(Golden("traffic_light.sc.json"), indented: true));
        var document = new StatechartDocumentViewModel("orig", path, Golden("traffic_light.sc.json"));

        document.TryRename("renamed");

        Assert.Equal("renamed", document.Name);
        Assert.False(File.Exists(path));
        Assert.True(File.Exists(Path.Combine(Path.GetDirectoryName(path)!, "renamed.sc.json")));
    }

    [Fact]
    public void Rename_To_An_Existing_Chart_Name_Is_Rejected()
    {
        var dir = Path.Combine(Path.GetTempPath(), "wz-sc-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        var aPath = Path.Combine(dir, "a.sc.json");
        File.WriteAllText(aPath, "{}");
        File.WriteAllText(Path.Combine(dir, "b.sc.json"), "{}");
        var document = new StatechartDocumentViewModel("a", aPath, Golden("traffic_light.sc.json"));

        document.TryRename("b");   // collides

        Assert.Equal("a", document.Name);
        Assert.True(File.Exists(aPath));
    }

    [Fact]
    public void A_Read_Only_Chart_Cannot_Be_Renamed()
    {
        var path = FreshChartPath("locked.sc.json");
        File.WriteAllText(path, "{}");
        var document = new StatechartDocumentViewModel("locked", path, Golden("traffic_light.sc.json")) { IsReadOnly = true };

        document.TryRename("unlocked");

        Assert.Equal("locked", document.Name);
        Assert.True(File.Exists(path));
    }

    [Fact]
    public void Delete_Chart_Removes_The_Files_And_Signals_The_Host()
    {
        var path = FreshChartPath("doomed.sc.json");
        File.WriteAllText(path, "{}");
        File.WriteAllText(Path.ChangeExtension(path, ".editor.json"), "{}");
        var document = new StatechartDocumentViewModel("doomed", path, Golden("traffic_light.sc.json"));
        var closed = false;
        document.Deleted = () => closed = true;

        document.DeleteChart();

        Assert.False(File.Exists(path));
        Assert.False(File.Exists(Path.ChangeExtension(path, ".editor.json")));
        Assert.True(closed);   // the host closes the tab
    }

    [Fact]
    public void A_Read_Only_Chart_Cannot_Be_Deleted()
    {
        var path = FreshChartPath("protected.sc.json");
        File.WriteAllText(path, "{}");
        var document = new StatechartDocumentViewModel("protected", path, Golden("traffic_light.sc.json")) { IsReadOnly = true };

        document.DeleteChart();

        Assert.True(File.Exists(path));   // protected
    }

    [Fact]
    public void An_Open_But_Empty_Chart_Reports_HasChart_For_The_Toolbars()
    {
        var chart = new Chart { Name = "empty" };
        var dataflow = new DataflowPaneViewModel();
        dataflow.Project(chart);

        Assert.True(dataflow.HasChart);    // a chart is loaded -> toolbar shows
        Assert.False(dataflow.HasGraph);   // ...even with no nodes yet
    }

    [Fact]
    public void Saved_Layout_Is_Restored_On_Reopen()
    {
        var path = FreshChartPath("traffic_light.sc.json");
        var first = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));

        var state = first.Control.States.First(s => s.StateId == "DELIBERATE");
        ((IEditorCanvas)first.Control).SelectOnly(state);
        ((IEditorCanvas)first.Control).MoveSelectedBy(120, 60);
        double movedX = state.X;
        first.Save();

        var reopened = new StatechartDocumentViewModel("traffic_light", path, Golden("traffic_light.sc.json"));
        var reopenedState = reopened.Control.States.First(s => s.StateId == "DELIBERATE");
        Assert.Equal(movedX, reopenedState.X, 3);
    }

    [Fact]
    public void Save_Refuses_A_Structurally_Invalid_Chart_And_Writes_Nothing()
    {
        // Delete every state: the control pane prunes the regions' state lists and then drops
        // the emptied regions, leaving a chart the engine's parse_chart refuses ("chart has no
        // regions"). Before Save validated, this wrote happily to .sc.json and the compiled IR
        // was pushed into every attached runner -- which then silently never started.
        var path = FreshChartPath("traffic_light.sc.json");
        File.WriteAllText(path, "SENTINEL");
        var document = new StatechartDocumentViewModel(
            "traffic_light", path, Golden("traffic_light.sc.json"));
        var canvas = (IEditorCanvas)document.Control;

        while (document.Control.States.Count > 0)
        {
            canvas.SelectOnly(document.Control.States[0]);
            canvas.DeleteSelected();
        }

        var ex = Assert.Throws<StatechartFormatException>(document.Save);
        Assert.Contains("no regions", ex.Message);

        // Nothing was written, and the document stays dirty so the user can fix and re-save --
        // a half-save that clobbered the good file with a broken chart would be the worst
        // outcome of the three.
        Assert.Equal("SENTINEL", File.ReadAllText(path));
        Assert.True(document.IsDirty);
    }

    [Fact]
    public void Save_Still_Writes_A_Chart_That_Stays_Valid_After_An_Edit()
    {
        // The gate must not be trigger-happy: deleting ONE of two states leaves a chart whose
        // region still has a state, so this is an ordinary successful save. (Guards against a
        // validation rule that rejects everything and makes the editor unusable.)
        var path = FreshChartPath("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel(
            "traffic_light", path, Golden("traffic_light.sc.json"));
        var canvas = (IEditorCanvas)document.Control;

        canvas.SelectOnly(document.Control.States.First(s => s.StateId == "HOLD"));
        canvas.DeleteSelected();

        document.Save();

        Assert.Single(StatechartJson.Load(File.ReadAllText(path)).States);
        Assert.False(document.IsDirty);
    }
}
