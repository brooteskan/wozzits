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

    // Build and show the behavior "+" menu. Re-queries the engine for its
    // currently registered modules (loaded asynchronously / changed by a
    // rebuild), then shows a MenuFlyout of plain MenuItems — same default theme
    // as the Components dropdown. The items are populated BEFORE ShowAt so the
    // presenter is built with them (populating after the popup opens renders an
    // empty menu).
    private void OnAddBehaviorClick(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button button
            || DataContext is not InspectorPaneViewModel vm)
        {
            return;
        }

        vm.RefreshBehaviorModuleCatalog();

        var flyout = new MenuFlyout();
        foreach (var option in vm.AvailableBehaviorModules)
        {
            flyout.Items.Add(new MenuItem
            {
                Header = option.Module,
                Command = option.AddCommand,
            });
        }

        flyout.ShowAt(button);
    }
}
