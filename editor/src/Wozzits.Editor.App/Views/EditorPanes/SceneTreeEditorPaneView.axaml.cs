using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.App.Views.EditorPanes;

// In-process drag payload for reparenting a scene node by dragging it onto a new
// parent (or onto empty space to move it to the top level).
internal static class SceneTreeDrag
{
    public static readonly DataFormat<SceneTreeNodeViewModel> NodeFormat =
        DataFormat.CreateInProcessFormat<SceneTreeNodeViewModel>(
            "wozzits-scene-node");
}

public partial class SceneTreeEditorPaneView : UserControl
{
    private SceneTreeNodeViewModel? _dragCandidate;
    private PointerPressedEventArgs? _dragPress;
    private Point _dragStart;

    public SceneTreeEditorPaneView()
    {
        InitializeComponent();
        AddHandler(
            InputElement.PointerMovedEvent,
            TreePointerMoved,
            RoutingStrategies.Tunnel | RoutingStrategies.Bubble,
            handledEventsToo: true);
        AddHandler(DragDrop.DragOverEvent, TreeDragOver);
        AddHandler(DragDrop.DropEvent, TreeDrop);
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

    private void AddChildClicked(object? sender, RoutedEventArgs e)
    {
        if (sender is MenuItem { DataContext: SceneTreeNodeViewModel node } &&
            DataContext is SceneTreeEditorPaneViewModel sceneTree)
        {
            sceneTree.AddChild(node);
        }
    }

    private void DeleteClicked(object? sender, RoutedEventArgs e)
    {
        if (sender is MenuItem { DataContext: SceneTreeNodeViewModel node } &&
            DataContext is SceneTreeEditorPaneViewModel sceneTree)
        {
            sceneTree.Remove(node);
        }
    }

    private void TreeKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Delete
            && DataContext is SceneTreeEditorPaneViewModel sceneTree
            && sceneTree.SelectedNode is { } node)
        {
            sceneTree.Remove(node);
            e.Handled = true;
        }
    }

    // A left-press on a tree row arms a drag candidate; a plain click (no move
    // past a small threshold) still selects, so dragging and selecting don't
    // conflict.
    private void NodePointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is Control { DataContext: SceneTreeNodeViewModel node }
            && e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
        {
            _dragCandidate = node;
            _dragPress = e;
            _dragStart = e.GetPosition(this);
        }
    }

    private async void TreePointerMoved(object? sender, PointerEventArgs e)
    {
        if (_dragCandidate is null)
        {
            return;
        }
        if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
        {
            _dragCandidate = null;
            return;
        }

        var position = e.GetPosition(this);
        if (Math.Abs(position.X - _dragStart.X) < 4.0
            && Math.Abs(position.Y - _dragStart.Y) < 4.0)
        {
            return;
        }

        var node = _dragCandidate;
        var press = _dragPress;
        _dragCandidate = null;
        _dragPress = null;
        if (press is null)
        {
            return;
        }

        var transfer = new DataTransfer();
        transfer.Add(DataTransferItem.Create(SceneTreeDrag.NodeFormat, node));
        await DragDrop.DoDragDropAsync(press, transfer, DragDropEffects.Move);
    }

    private void TreeDragOver(object? sender, DragEventArgs e)
    {
        e.DragEffects = e.DataTransfer.Contains(SceneTreeDrag.NodeFormat)
            ? DragDropEffects.Move
            : DragDropEffects.None;
        e.Handled = true;
    }

    private void TreeDrop(object? sender, DragEventArgs e)
    {
        if (DataContext is not SceneTreeEditorPaneViewModel sceneTree
            || e.DataTransfer.TryGetValue(SceneTreeDrag.NodeFormat) is not { } dragged)
        {
            return;
        }

        // Drop target is the node under the pointer, or null (top level) when the
        // drop lands on empty tree space.
        sceneTree.Reparent(dragged, FindNode(e.Source));
        e.Handled = true;
    }

    private static SceneTreeNodeViewModel? FindNode(object? source)
    {
        var current = source as StyledElement;
        while (current is not null)
        {
            if (current.DataContext is SceneTreeNodeViewModel node)
            {
                return node;
            }
            current = current.Parent;
        }
        return null;
    }
}
