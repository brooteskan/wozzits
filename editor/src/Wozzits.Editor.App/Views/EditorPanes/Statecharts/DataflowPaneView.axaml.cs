using Avalonia.Controls;

namespace Wozzits.Editor.App.Views.EditorPanes.Statecharts;

// Render-only dataflow canvas for E2b (pan/zoom via the ScrollViewer + ScaleTransform).
// Node dragging, selection, and connect gestures come with E2c.
public partial class DataflowPaneView : UserControl
{
    public DataflowPaneView()
    {
        InitializeComponent();
    }
}
