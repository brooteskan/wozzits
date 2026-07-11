namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.IO;
using Wozzits.Editor.Statecharts;

// One chart, both layers, one document. Owns the Chart and its .sc.json path, so it can save
// edits back to the chart and hand-placed layout to a <chart>.sc.editor.json sidecar. The
// control and dataflow canvases share the one Chart instance (edits land in one chart).
public sealed class StatechartDocumentViewModel : ViewModelBase
{
    private readonly Chart _chart;
    private readonly string _path;

    public StatechartDocumentViewModel(string name, string path, Chart chart)
    {
        Name = name;
        _path = path;
        _chart = chart;
        Control = new ControlPaneViewModel();
        Dataflow = new DataflowPaneViewModel();
        Control.Project(chart);
        Dataflow.Project(chart);

        // Cross-layer focus: selecting a state on the control canvas dims the dataflow to
        // just the ops feeding that state (David's "simplify the dataflow by selection").
        Control.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(ControlPaneViewModel.SelectedState))
            {
                Dataflow.FocusOnState(Control.SelectedState?.Model);
            }
        };

        ApplySavedLayout();
    }

    public string Name { get; }

    public ControlPaneViewModel Control { get; }

    public DataflowPaneViewModel Dataflow { get; }

    // The chart was edited (needs a .sc.json rewrite) or the layout moved (needs the sidecar).
    public bool IsDirty => Control.IsDirty || Control.IsLayoutDirty || Dataflow.IsDirty || Dataflow.IsLayoutDirty;

    private string LayoutPath => Path.ChangeExtension(_path, ".editor.json");

    // Write the chart back to its .sc.json (only when actually edited, so an untouched chart is
    // never reformatted) and the hand-placed layout to the editor-owned sidecar.
    public void Save()
    {
        if (Control.IsDirty || Dataflow.IsDirty)
        {
            File.WriteAllText(_path, StatechartJson.Emit(_chart, indented: true));
        }

        File.WriteAllText(LayoutPath, CaptureLayout().ToJson());
        Control.ClearDirty();
        Dataflow.ClearDirty();
    }

    private StatechartLayout CaptureLayout()
    {
        var layout = new StatechartLayout
        {
            ControlZoom = Control.Zoom,
            DataflowZoom = Dataflow.Zoom,
        };
        foreach (var state in Control.States)
        {
            layout.StatePositions[state.StateId] = new StatechartLayout.Point(state.X, state.Y);
        }
        foreach (var node in Dataflow.Nodes)
        {
            layout.NodePositions[node.NodeId] = new StatechartLayout.Point(node.X, node.Y);
        }
        return layout;
    }

    private void ApplySavedLayout()
    {
        if (!File.Exists(LayoutPath))
        {
            return;
        }

        string json;
        try
        {
            json = File.ReadAllText(LayoutPath);
        }
        catch (IOException)
        {
            return;
        }

        var layout = StatechartLayout.FromJson(json);
        Control.ApplyLayout(layout);
        Dataflow.ApplyLayout(layout);
    }
}
