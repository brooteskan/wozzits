using Avalonia.Controls;
using Wozzits.Editor.App.Controls;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.App.Views.EditorPanes.Minds;

// The mind graph canvas. Navigation + selection + node-drag come from the shared
// GraphInteraction controller (the same one the statechart panes use). No wiring preview
// yet -- drawing bonds by drag is the next seam.
public partial class MindPaneView : UserControl
{
    public MindPaneView()
    {
        InitializeComponent();
        _ = new GraphInteraction(this, Scroll, Canvas, SelectionRectangle, () => DataContext as IEditorCanvas);
    }
}
