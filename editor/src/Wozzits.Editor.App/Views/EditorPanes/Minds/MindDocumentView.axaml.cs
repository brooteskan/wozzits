using Avalonia.Controls;

namespace Wozzits.Editor.App.Views.EditorPanes.Minds;

// One mind, one document: a header (name + delete) over the mind graph canvas. The pane
// resolves to MindPaneView via the ViewLocator (namespace/suffix convention).
public partial class MindDocumentView : UserControl
{
    public MindDocumentView()
    {
        InitializeComponent();
    }
}
