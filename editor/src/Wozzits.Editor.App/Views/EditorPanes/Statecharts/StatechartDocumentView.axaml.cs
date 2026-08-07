using Avalonia.Controls;

namespace Wozzits.Editor.App.Views.EditorPanes.Statecharts;

// One chart document: control canvas over dataflow canvas, split by a resizable divider.
public partial class StatechartDocumentView : UserControl
{
    public StatechartDocumentView()
    {
        InitializeComponent();
    }
}
