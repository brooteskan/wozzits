namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using Wozzits.Editor.Statecharts;

// One chart, both layers. A single document hosts the control canvas and the dataflow
// canvas over the SAME Chart instance (so edits land in one chart and save as one), shown
// split with a resizable divider. The existing ControlPaneView / DataflowPaneView render
// the two sub-view-models via the app ViewLocator.
public sealed class StatechartDocumentViewModel : ViewModelBase
{
    public StatechartDocumentViewModel(string name, Chart chart)
    {
        Name = name;
        Control = new ControlPaneViewModel();
        Dataflow = new DataflowPaneViewModel();
        Control.Project(chart);
        Dataflow.Project(chart);
    }

    public string Name { get; }

    public ControlPaneViewModel Control { get; }

    public DataflowPaneViewModel Dataflow { get; }
}
