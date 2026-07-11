using Avalonia.Controls;

namespace Wozzits.Editor.App.Views.EditorPanes.Statecharts;

// Render-only control canvas for E3b (pan/zoom via the ScrollViewer + ScaleTransform).
// State-box dragging, selection, and transition-authoring gestures come with E3c.
public partial class ControlPaneView : UserControl
{
    public ControlPaneView()
    {
        InitializeComponent();
    }
}
