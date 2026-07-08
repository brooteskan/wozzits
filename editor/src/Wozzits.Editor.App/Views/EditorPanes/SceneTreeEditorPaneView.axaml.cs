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
        foreach (var item in menu.Items.OfType<MenuItem>())
        {
            item.IsEnabled = canEdit;
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
