using Avalonia.Controls;
using Wozzits.Editor.App.Controls;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.App.Views.EditorPanes.Statecharts;

// Control canvas. Navigation + selection + state-drag come from the shared GraphInteraction
// controller; the TransitionPreview line lets it also drive the armed state->state
// transition-draw gesture (M3).
public partial class ControlPaneView : UserControl
{
    public ControlPaneView()
    {
        InitializeComponent();
        _ = new GraphInteraction(this, Scroll, Canvas, SelectionRectangle, () => DataContext as IEditorCanvas, TransitionPreview);
    }
}
