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
}
