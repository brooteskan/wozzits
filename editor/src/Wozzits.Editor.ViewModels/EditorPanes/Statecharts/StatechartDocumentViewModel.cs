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

        // Cross-layer focus: selecting a state on the control canvas dims the dataflow to
        // just the ops feeding that state (David's "simplify the dataflow by selection").
        Control.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(ControlPaneViewModel.SelectedState))
            {
                Dataflow.FocusOnState(Control.SelectedState?.Model);
            }
        };
    }

    public string Name { get; }

    public ControlPaneViewModel Control { get; }

    public DataflowPaneViewModel Dataflow { get; }
}
