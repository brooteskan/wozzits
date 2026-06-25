using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.App.Views.EditorPanes;

public partial class InspectorPaneView : UserControl
{
    public InspectorPaneView()
    {
        InitializeComponent();

        // Persist scene edits to disk when an editable field loses focus, rather
        // than per keystroke (the live push already mirrors keystrokes into the
        // running viewport). LostFocus bubbles, so one handler on the root covers
        // every field; save_scene is dirty-gated, so blurs from non-scene fields
        // (e.g. asset-graph params) are no-ops.
        AddHandler(InputElement.LostFocusEvent, OnFieldLostFocus, RoutingStrategies.Bubble);
    }

    private void OnFieldLostFocus(object? sender, RoutedEventArgs e)
    {
        if (e.Source is TextBox && DataContext is InspectorPaneViewModel vm)
        {
            vm.PersistSceneEditsOnBlur();
        }
    }
}
