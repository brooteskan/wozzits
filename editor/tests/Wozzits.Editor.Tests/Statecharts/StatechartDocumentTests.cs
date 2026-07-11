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
}
