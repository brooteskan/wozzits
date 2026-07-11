using Avalonia.Controls;
using Wozzits.Editor.App.Controls;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.App.Views.EditorPanes.Statecharts;

// Dataflow canvas. Navigation + selection + node-drag come from the shared GraphInteraction
// controller; the WirePreview line lets it also drive the output->input wiring gesture (M2).
public partial class DataflowPaneView : UserControl
{
    public DataflowPaneView()
    {
        InitializeComponent();
        _ = new GraphInteraction(this, Scroll, Canvas, SelectionRectangle, () => DataContext as IEditorCanvas, WirePreview);
    }
}
