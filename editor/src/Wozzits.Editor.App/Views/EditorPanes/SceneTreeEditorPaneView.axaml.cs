using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
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
        AddHandler(DragDrop.DragLeaveEvent, TreeDragLeave);
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

    // Before the scene-node context menu opens, disable its editing items when no
    // viewport runtime is running — those edits (add child / export / delete) act
    // on the live runtime. Re-evaluated each open because the runtime can stop
    // asynchronously (its window closed) with no push notification. The VM re-
    // checks too, so the Delete-key and drag-drop paths stay gated as well.
    private void SceneNodeContextMenuOpening(object? sender, CancelEventArgs e)
    {
        if (sender is not ContextMenu menu
            || DataContext is not SceneTreeEditorPaneViewModel sceneTree)
        {
            return;
        }

        var canEdit = sceneTree.CanEditScene;
        var node = menu.DataContext as SceneTreeNodeViewModel;
        foreach (var item in menu.Items.OfType<MenuItem>())
        {
            // The reorder items additionally grey out at the ends of the node's
            // sibling group (nothing to move past); everything else is gated only
            // by whether the viewport is running.
            item.IsEnabled = (item.Tag as string) switch
            {
                "reorder-up" => canEdit && node is not null && sceneTree.CanMoveUp(node),
                "reorder-down" => canEdit && node is not null && sceneTree.CanMoveDown(node),
                _ => canEdit,
            };
        }
    }

    private void MoveUpClicked(object? sender, RoutedEventArgs e)
    {
        if (sender is MenuItem { DataContext: SceneTreeNodeViewModel node } &&
            DataContext is SceneTreeEditorPaneViewModel sceneTree)
        {
            sceneTree.MoveUp(node);
        }
    }

    private void MoveDownClicked(object? sender, RoutedEventArgs e)
    {
        if (sender is MenuItem { DataContext: SceneTreeNodeViewModel node } &&
            DataContext is SceneTreeEditorPaneViewModel sceneTree)
        {
            sceneTree.MoveDown(node);
        }
    }

    private void AddChildClicked(object? sender, RoutedEventArgs e)
    {
        if (sender is MenuItem { DataContext: SceneTreeNodeViewModel node } &&
            DataContext is SceneTreeEditorPaneViewModel sceneTree)
        {
            sceneTree.AddChild(node);
        }
    }

    // Open a save-file dialog (defaulting to the project's scenelets/ folder, with
    // a node-derived suggested name) and, on confirm, export the node's subtree as
    // a standalone scene.json through the engine session.
    private async void ExportSubtreeClicked(object? sender, RoutedEventArgs e)
    {
        if (sender is not MenuItem { DataContext: SceneTreeNodeViewModel node } ||
            DataContext is not SceneTreeEditorPaneViewModel sceneTree)
        {
            return;
        }

        var storageProvider = TopLevel.GetTopLevel(this)?.StorageProvider;
        if (storageProvider is null)
        {
            return;
        }

        IStorageFolder? startFolder = null;
        var scenelets = sceneTree.EnsureSceneletsDirectory();
        if (scenelets is not null)
        {
            startFolder = await storageProvider.TryGetFolderFromPathAsync(scenelets);
        }

        var file = await storageProvider.SaveFilePickerAsync(new FilePickerSaveOptions
        {
            Title = "Export subtree as scene",
            SuggestedFileName =
                SceneTreeEditorPaneViewModel.SuggestExportFileName(node),
            SuggestedStartLocation = startFolder,
            DefaultExtension = "json",
            FileTypeChoices = new List<FilePickerFileType>
            {
                new("Scene")
                {
                    Patterns = ["*.scene.json", "*.json"],
                },
            },
        });

        var path = file?.TryGetLocalPath();
        if (string.IsNullOrEmpty(path))
        {
            return;
        }

        sceneTree.ExportSubtree(node, path);
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
        e.Handled = true;
        if (!e.DataTransfer.Contains(SceneTreeDrag.NodeFormat))
        {
            e.DragEffects = DragDropEffects.None;
            HideDropIndicator();
            return;
        }

        e.DragEffects = DragDropEffects.Move;
        var (row, _, position) = ResolveDrop(e);
        if (row is null)
        {
            HideDropIndicator();  // empty space => reparent to top level, no marker
            return;
        }
        UpdateDropIndicator(row, position);
    }

    private void TreeDrop(object? sender, DragEventArgs e)
    {
        HideDropIndicator();
        if (DataContext is not SceneTreeEditorPaneViewModel sceneTree
            || e.DataTransfer.TryGetValue(SceneTreeDrag.NodeFormat) is not { } dragged)
        {
            return;
        }

        // Onto a row => reparent under that node; between rows => reorder to that
        // draw slot. Empty space (no row) => reparent to the top level.
        var (_, target, position) = ResolveDrop(e);
        sceneTree.DropNode(dragged, target, position);
        e.Handled = true;
    }

    private void TreeDragLeave(object? sender, DragEventArgs e)
    {
        HideDropIndicator();
    }

    // Resolve the drop under the pointer: the target row control, its node, and
    // whether the pointer is in the row's top band (Before), bottom band (After),
    // or middle (Into). No row (dropped on empty tree space) => (null, null, Into),
    // which the drop treats as "reparent to top level".
    private (Control? Row, SceneTreeNodeViewModel? Node, SceneTreeDropPosition Position)
        ResolveDrop(DragEventArgs e)
    {
        var row = FindRow(e.Source);
        if (row is null || row.DataContext is not SceneTreeNodeViewModel node)
        {
            return (null, null, SceneTreeDropPosition.Into);
        }

        var height = row.Bounds.Height;
        var y = e.GetPosition(row).Y;
        // Generous middle band so onto-node (reparent) stays easy to hit; the
        // top/bottom ~30% are the between-rows (reorder) zones.
        var position = height <= 0.0
            ? SceneTreeDropPosition.Into
            : y < height * 0.3
                ? SceneTreeDropPosition.Before
                : y > height * 0.7
                    ? SceneTreeDropPosition.After
                    : SceneTreeDropPosition.Into;
        return (row, node, position);
    }

    private void UpdateDropIndicator(Control row, SceneTreeDropPosition position)
    {
        if (row.TranslatePoint(new Point(0, 0), DropIndicatorCanvas) is not { } topLeft)
        {
            HideDropIndicator();
            return;
        }

        if (position == SceneTreeDropPosition.Into)
        {
            DropInsertionLine.IsVisible = false;
            Canvas.SetLeft(DropIntoHighlight, topLeft.X);
            Canvas.SetTop(DropIntoHighlight, topLeft.Y);
            DropIntoHighlight.Width = row.Bounds.Width;
            DropIntoHighlight.Height = row.Bounds.Height;
            DropIntoHighlight.IsVisible = true;
            return;
        }

        DropIntoHighlight.IsVisible = false;
        var lineY = position == SceneTreeDropPosition.Before
            ? topLeft.Y
            : topLeft.Y + row.Bounds.Height;
        Canvas.SetLeft(DropInsertionLine, 0);
        Canvas.SetTop(DropInsertionLine, lineY - 1.0);  // centre the 2px line
        DropInsertionLine.Width = DropIndicatorCanvas.Bounds.Width;
        DropInsertionLine.IsVisible = true;
    }

    private void HideDropIndicator()
    {
        DropInsertionLine.IsVisible = false;
        DropIntoHighlight.IsVisible = false;
    }

    // The row content control (the template-root StackPanel carrying the node as
    // its DataContext) at or above `source`; null when the pointer is not over a
    // row. Walks the logical tree like the old FindNode, but returns the row
    // element (for its bounds) rather than just the node.
    private static Control? FindRow(object? source)
    {
        var current = source as StyledElement;
        while (current is not null)
        {
            if (current is StackPanel { DataContext: SceneTreeNodeViewModel } row)
            {
                return row;
            }
            current = current.Parent;
        }
        return null;
    }
}
