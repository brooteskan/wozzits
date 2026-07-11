using Avalonia.Controls;
using Wozzits.Editor.App.Controls;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.App.Views.EditorPanes.Statecharts;

// Dataflow canvas. Navigation + selection + node-drag come from the shared GraphInteraction
// controller (E2c phase 1); structural editing (add/remove/wire) + save arrive later.
public partial class DataflowPaneView : UserControl
{
    public DataflowPaneView()
    {
        InitializeComponent();
        _ = new GraphInteraction(this, Scroll, Canvas, SelectionRectangle, () => DataContext as IEditorCanvas);
    }
}
