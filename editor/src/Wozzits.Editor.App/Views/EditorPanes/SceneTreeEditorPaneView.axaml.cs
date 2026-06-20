using Avalonia.Controls;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.App.Views.EditorPanes;

public partial class SceneTreeEditorPaneView : UserControl
{
    public SceneTreeEditorPaneView()
    {
        InitializeComponent();
    }

    private void SceneTreeSelectionChanged(object? sender, SelectionChangedEventArgs e)
    {
        if (sender is not TreeView { SelectedItem: SceneTreeNodeViewModel node } ||
            DataContext is not SceneTreeEditorPaneViewModel sceneTree)
        {
            return;
        }

        sceneTree.SelectNode(node);
    }
}
