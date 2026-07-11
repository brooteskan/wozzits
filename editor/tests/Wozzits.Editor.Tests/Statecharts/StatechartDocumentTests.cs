using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.Tests.Statecharts;

/// <summary>The combined chart document projects both canvases from one shared chart.</summary>
public sealed class StatechartDocumentTests
{
    private static Chart Golden(string file) =>
        StatechartJson.Load(File.ReadAllText(Path.Combine(CorpusLocator.StatechartsDir(), file)));

    [Fact]
    public void Document_Projects_Both_Canvases_From_One_Chart()
    {
        var chart = Golden("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", chart);

        Assert.True(document.Control.HasGraph);
        Assert.True(document.Dataflow.HasGraph);
        Assert.Equal(2, document.Control.States.Count);
        Assert.Equal(15, document.Dataflow.Nodes.Count);
    }

    [Fact]
    public void Both_Canvases_Share_The_Same_Chart_Instance()
    {
        // A control-side edit (delete a state) mutates the one chart; the dataflow layer
        // (independent pure ops) is unaffected but still references the same chart.
        var chart = Golden("traffic_light.sc.json");
        var document = new StatechartDocumentViewModel("traffic_light", chart);

        ((IEditorCanvas)document.Control).SelectOnly(document.Control.States[0]);
        ((IEditorCanvas)document.Control).DeleteSelected();

        Assert.Single(document.Control.States);
        Assert.True(document.Control.IsDirty);
        Assert.True(document.Dataflow.HasGraph);   // dataflow still projects fine
    }

    [Fact]
    public void Selecting_A_State_Dims_The_Dataflow_That_Does_Not_Feed_It()
    {
        var document = new StatechartDocumentViewModel("traffic_light", Golden("traffic_light.sc.json"));
        var delib = document.Control.States.First(s => s.StateId == "DELIBERATE");

        ((IEditorCanvas)document.Control).SelectOnly(delib);

        // z (marginal) feeds DELIBERATE's scales via s0d<-p0<-md0<-z; sig feeds it via reads.
        Assert.False(document.Dataflow.Nodes.First(n => n.NodeId == "z").IsDimmed);
        Assert.False(document.Dataflow.Nodes.First(n => n.NodeId == "sig").IsDimmed);
        // s0h / c feed only HOLD, so they dim.
        Assert.True(document.Dataflow.Nodes.First(n => n.NodeId == "s0h").IsDimmed);
        Assert.True(document.Dataflow.Nodes.First(n => n.NodeId == "c").IsDimmed);
    }

    [Fact]
    public void Deselecting_Restores_The_Full_Dataflow()
    {
        var document = new StatechartDocumentViewModel("traffic_light", Golden("traffic_light.sc.json"));
        var canvas = (IEditorCanvas)document.Control;

        canvas.SelectOnly(document.Control.States.First(s => s.StateId == "DELIBERATE"));
        Assert.Contains(document.Dataflow.Nodes, n => n.IsDimmed);

        canvas.ClearSelection();
        Assert.DoesNotContain(document.Dataflow.Nodes, n => n.IsDimmed);
    }
}
