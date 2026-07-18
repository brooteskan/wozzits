using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.App.Views.EditorPanes;

// Drag payload for moving nodes BETWEEN graph panes -- dock a sub-graph beside the root
// canvas (or another sub-graph) and drag nodes across to regroup them. In-process only,
// like the asset browser's schema drag (issue woguls/wozzits-editor#1).
internal static class AssetGraphNodeDrag
{
    public static readonly DataFormat<AssetGraphNodeDragPayload> NodesFormat =
        DataFormat.CreateInProcessFormat<AssetGraphNodeDragPayload>(
            "wozzits-asset-graph-nodes");
}

// The dragged nodes, each with its offset from the card under the cursor. The source pane
// needs no mention: the drop target raises GraphMutated and the shell re-pulls every other
// pane, so the node leaving its old canvas takes care of itself.
internal sealed record AssetGraphNodeDragPayload(
    IReadOnlyList<AssetGraphNodeDrop> Nodes);

public partial class AssetGraphEditorPaneView : UserControl
{
    private static readonly TimeSpan ZoomPersistInterval = TimeSpan.FromSeconds(15);
    // How far past the pane edge the pointer must travel before an in-canvas node move
    // turns into a cross-pane drag.
    private const double CrossPaneDragMargin = 12.0;
    private readonly DispatcherTimer _zoomPersistTimer;
    private AssetGraphNodeCardViewModel? _dragNode;
    private Control? _dragControl;
    // Where the dragged nodes sat when the in-canvas move began. If the pointer leaves the
    // pane the move becomes a cross-pane drag, and the source has to be put back: from that
    // point the landing position is the drop target's to decide, and a cancelled drag must
    // leave the source canvas untouched.
    private List<(AssetGraphNodeCardViewModel Node, double X, double Y)>? _dragStartPositions;
    // DoDragDropAsync only accepts the PRESS args, but a cross-pane drag is not recognised
    // until the pointer has moved out of the pane -- so hold the press that started the
    // gesture and hand it over when the move escalates.
    private PointerPressedEventArgs? _dragPressArgs;
    private AssetGraphSubGraph? _dragProxy;
    private Control? _dragProxyControl;
    private Avalonia.Point _lastPointerPosition;
    private bool _isPanning;
    private Avalonia.Point _panStartPointerPosition;
    private Avalonia.Vector _panStartOffset;
    private bool _didRightDrag;
    private object? _rightDownSource;
    private bool _isBoxSelecting;
    private Avalonia.Point _boxSelectStartGraphPosition;
    private Avalonia.Point _boxSelectCurrentGraphPosition;
    private bool _hasPendingZoomPersist;
    private AssetGraphNodeCardViewModel? _connectionFromNode;
    private AssetGraphPortViewModel? _connectionFromPort;

    public AssetGraphEditorPaneView()
    {
        InitializeComponent();
        _zoomPersistTimer = new DispatcherTimer
        {
            Interval = ZoomPersistInterval,
        };
        _zoomPersistTimer.Tick += ZoomPersistTimerTick;
        AddHandler(
            InputElement.PointerPressedEvent,
            GraphPointerPressed,
            RoutingStrategies.Tunnel,
            handledEventsToo: true);
        AddHandler(
            InputElement.PointerMovedEvent,
            GraphPointerMoved,
            RoutingStrategies.Tunnel,
            handledEventsToo: true);
        AddHandler(
            InputElement.PointerReleasedEvent,
            GraphPointerReleased,
            RoutingStrategies.Tunnel,
            handledEventsToo: true);
        AddHandler(
            InputElement.PointerWheelChangedEvent,
            GraphPointerWheelChanged,
            RoutingStrategies.Tunnel,
            handledEventsToo: true);
        AddHandler(DragDrop.DragOverEvent, GraphDragOver);
        AddHandler(DragDrop.DropEvent, GraphDrop);
        AddHandler(
            InputElement.KeyDownEvent,
            GraphKeyDown,
            RoutingStrategies.Tunnel | RoutingStrategies.Bubble,
            handledEventsToo: true);
        Focusable = true;
    }

    private void GraphKeyDown(object? sender, KeyEventArgs e)
    {
        if (DataContext is not AssetGraphEditorPaneViewModel graph)
        {
            return;
        }

        if ((e.Key == Key.Delete || e.Key == Key.Back) && graph.SelectedNodes.Count > 0)
        {
            graph.RemoveSelectedNodes();
            e.Handled = true;
            return;
        }

        // Ctrl+G groups the selection into a sub-graph; Ctrl+Shift+G ungroups the selected
        // proxy. (The right-click "Collapse into sub-graph" menu arrives with the drill-in
        // tab in a later seam.)
        if (e.Key == Key.G && e.KeyModifiers.HasFlag(KeyModifiers.Control))
        {
            e.Handled = e.KeyModifiers.HasFlag(KeyModifiers.Shift)
                ? graph.UngroupSelectedSubGraph()
                : graph.CreateSubGraphFromSelection("Sub-graph") is not null;
        }
    }

    private void GraphDragOver(object? sender, DragEventArgs e)
    {
        if (e.DataTransfer.Contains(AssetBrowserDrag.SchemaFormat))
        {
            e.DragEffects = DragDropEffects.Copy;
        }
        else if (e.DataTransfer.Contains(AssetGraphNodeDrag.NodesFormat))
        {
            e.DragEffects = DragDropEffects.Move;
        }
        else
        {
            e.DragEffects = DragDropEffects.None;
        }

        e.Handled = true;
    }

    private void GraphDrop(object? sender, DragEventArgs e)
    {
        if (DataContext is not AssetGraphEditorPaneViewModel graph)
        {
            return;
        }

        // Drop point in graph content space (the canvas carries the zoom
        // transform, so GetPosition returns pre-zoom graph coordinates).
        var position = e.GetPosition(AssetGraphCanvas);
        const double cardWidthHalf = 110.0;
        const double cardHeightHalf = 58.0;

        // Nodes dragged over from another graph pane: regroup them onto this canvas. The
        // offsets are measured from the dragged card's top-left, so anchor the drop there.
        if (e.DataTransfer.TryGetValue(AssetGraphNodeDrag.NodesFormat) is { } nodes)
        {
            graph.AcceptDroppedNodes(
                nodes.Nodes,
                position.X - cardWidthHalf,
                position.Y - cardHeightHalf);
            e.Handled = true;
            return;
        }

        if (e.DataTransfer.TryGetValue(AssetBrowserDrag.SchemaFormat) is not { } schema)
        {
            return;
        }

        graph.AddNodeAt(
            schema.Schema,
            (uint)schema.Type,
            position.X - cardWidthHalf,
            position.Y - cardHeightHalf);
        e.Handled = true;
    }

    private void NodeCardPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control control
            || control.DataContext is not AssetGraphNodeCardViewModel node
            || DataContext is not AssetGraphEditorPaneViewModel graph)
        {
            return;
        }

        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        if (e.KeyModifiers.HasFlag(KeyModifiers.Shift))
        {
            graph.ToggleNodeSelection(node);
        }
        else if (!node.IsSelected)
        {
            graph.SelectNode(node);
        }

        if (!node.IsSelected)
        {
            e.Handled = true;
            return;
        }

        _dragNode = node;
        _dragControl = control;
        // The press above guarantees the node is selected, so the selection IS the drag set
        // (the same set MoveSelectedNodesByGraphDelta will move).
        _dragStartPositions = [.. graph.SelectedNodes.Select(
            selected => (Node: selected, selected.X, selected.Y))];
        _dragPressArgs = e;
        _lastPointerPosition = ToGraphPosition(e);
        e.Pointer.Capture(control);
        Focus();
        e.Handled = true;
    }

    private async void NodeCardPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_dragNode is null
            || _dragControl is null
            || e.Pointer.Captured != _dragControl
            || DataContext is not AssetGraphEditorPaneViewModel graph)
        {
            return;
        }

        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        // Dragged clear of this pane: hand the move over to whichever pane it is dropped
        // on. The pointer is captured by the card, so moves keep arriving out here. The
        // margin keeps a drag that merely grazes the edge an ordinary in-canvas move.
        var local = e.GetPosition(this);
        if (local.X < -CrossPaneDragMargin
            || local.Y < -CrossPaneDragMargin
            || local.X > Bounds.Width + CrossPaneDragMargin
            || local.Y > Bounds.Height + CrossPaneDragMargin)
        {
            e.Handled = true;
            await BeginCrossPaneNodeDrag(graph);
            return;
        }

        var current = ToGraphPosition(e);
        graph.MoveSelectedNodesByGraphDelta(
            _dragNode,
            current.X - _lastPointerPosition.X,
            current.Y - _lastPointerPosition.Y);
        _lastPointerPosition = current;
        e.Handled = true;
    }

    // Escalate an in-canvas node move into a real drag/drop once it leaves the pane, so it
    // can land on another graph pane docked alongside.
    private async Task BeginCrossPaneNodeDrag(AssetGraphEditorPaneViewModel graph)
    {
        var anchor = _dragNode;
        var press = _dragPressArgs;
        if (anchor is null || press is null)
        {
            return;
        }

        // Put the nodes back BEFORE measuring, so the offsets describe the layout the user
        // started from rather than wherever the abandoned in-canvas drag left them.
        RestoreDragStartPositions();

        var payload = new AssetGraphNodeDragPayload(
            [.. graph.SelectedNodes.Select(selected => new AssetGraphNodeDrop(
                selected.Id,
                selected.X - anchor.X,
                selected.Y - anchor.Y))]);

        press.Pointer.Capture(null);
        _dragNode = null;
        _dragControl = null;
        _dragPressArgs = null;

        var transfer = new DataTransfer();
        transfer.Add(DataTransferItem.Create(AssetGraphNodeDrag.NodesFormat, payload));
        await DragDrop.DoDragDropAsync(press, transfer, DragDropEffects.Move);
    }

    private void RestoreDragStartPositions()
    {
        if (_dragStartPositions is null)
        {
            return;
        }

        foreach (var (node, x, y) in _dragStartPositions)
        {
            node.X = x;
            node.Y = y;
        }

        _dragStartPositions = null;
    }

    private void NodeCardPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (_dragNode is not null
            && DataContext is AssetGraphEditorPaneViewModel graph)
        {
            graph.CommitSelectedNodePositions(_dragNode);
        }

        e.Pointer.Capture(null);
        _dragNode = null;
        _dragControl = null;
        _dragStartPositions = null;
        _dragPressArgs = null;
        e.Handled = true;
    }

    private void SubGraphProxyPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control control
            || control.DataContext is not AssetGraphSubGraph subGraph
            || DataContext is not AssetGraphEditorPaneViewModel graph)
        {
            return;
        }

        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        if (e.ClickCount == 2)
        {
            graph.OpenSubGraph(subGraph);
            e.Handled = true;
            return;
        }

        graph.SelectSubGraph(subGraph);
        _dragProxy = subGraph;
        _dragProxyControl = control;
        _lastPointerPosition = ToGraphPosition(e);
        e.Pointer.Capture(control);
        Focus();
        e.Handled = true;
    }

    private void SubGraphProxyPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_dragProxy is null
            || _dragProxyControl is null
            || e.Pointer.Captured != _dragProxyControl
            || DataContext is not AssetGraphEditorPaneViewModel graph)
        {
            return;
        }

        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        var current = ToGraphPosition(e);
        graph.MoveSubGraphProxyByGraphDelta(
            _dragProxy,
            current.X - _lastPointerPosition.X,
            current.Y - _lastPointerPosition.Y);
        _lastPointerPosition = current;
        e.Handled = true;
    }

    private void SubGraphProxyPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (_dragProxy is null)
        {
            return;
        }

        e.Pointer.Capture(null);
        _dragProxy = null;
        _dragProxyControl = null;
        e.Handled = true;
    }

    private void OutputPortPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        BeginConnectionDragFromEvent(e);
    }

    private void InputPortPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (_connectionFromNode is null
            || _connectionFromPort is null
            || sender is not Control { DataContext: AssetGraphPortViewModel port }
            || !port.IsInput
            || DataContext is not AssetGraphEditorPaneViewModel graph)
        {
            return;
        }

        graph.ConnectToInputPort(_connectionFromNode, port);
        _connectionFromNode = null;
        _connectionFromPort = null;
        e.Handled = true;
    }

    private void GraphPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (!IsPointerInsideGraphViewport(e))
        {
            return;
        }

        var point = e.GetCurrentPoint(this);
        if (point.Properties.IsLeftButtonPressed
            && BeginConnectionDragFromEvent(e))
        {
            return;
        }

        if (point.Properties.IsRightButtonPressed)
        {
            if (e.KeyModifiers.HasFlag(KeyModifiers.Alt)
                && DataContext is AssetGraphEditorPaneViewModel graphView)
            {
                var tolerance = Math.Max(6.0, 8.0 / graphView.Zoom);
                var edge = WireLayer.HitTestEdge(ToGraphPosition(e), tolerance);
                if (edge is not null)
                {
                    graphView.DisconnectEdge(edge);
                    e.Handled = true;
                    return;
                }
            }

            // Right-drag pans; a right-click that never moves opens the context menu
            // (tracked via _didRightDrag). Remember what was under the pointer at press
            // time — pointer capture makes the release event's Source unreliable.
            _didRightDrag = false;
            _rightDownSource = e.Source;
            _isPanning = true;
            _panStartPointerPosition = point.Position;
            _panStartOffset = AssetGraphScrollViewer.Offset;
            e.Pointer.Capture(this);
            e.Handled = true;
            return;
        }

        if (!point.Properties.IsLeftButtonPressed
            || DataContext is not AssetGraphEditorPaneViewModel graph
            || NodeUnderPointer(e.Source) is not null
            || SubGraphUnderPointer(e.Source) is not null)
        {
            return;
        }

        Focus();
        _isBoxSelecting = true;
        _boxSelectStartGraphPosition = ToGraphPosition(e);
        _boxSelectCurrentGraphPosition = _boxSelectStartGraphPosition;
        UpdateSelectionRectangle();
        e.Pointer.Capture(this);
        e.Handled = true;
    }

    private void GraphPointerMoved(object? sender, PointerEventArgs e)
    {
        if (_connectionFromNode is not null
            && DataContext is AssetGraphEditorPaneViewModel graph)
        {
            var graphPosition = ToGraphPosition(e);
            graph.UpdateConnectionPreviewEnd(graphPosition.X, graphPosition.Y);
            graph.PreviewConnectionTarget(
                _connectionFromNode,
                graph.FindInputPortAt(graphPosition.X, graphPosition.Y));
            e.Handled = true;
            return;
        }

        if (!_isPanning || e.Pointer.Captured != this)
        {
            UpdateBoxSelectionDrag(e);
            return;
        }

        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsRightButtonPressed)
        {
            FinishPanning(e.Pointer);
            return;
        }

        var delta = point.Position - _panStartPointerPosition;
        if (Math.Abs(delta.X) > 3.0 || Math.Abs(delta.Y) > 3.0)
        {
            _didRightDrag = true;
        }

        var maxOffsetX = Math.Max(
            0.0,
            AssetGraphScrollViewer.Extent.Width
                - AssetGraphScrollViewer.Viewport.Width);
        var maxOffsetY = Math.Max(
            0.0,
            AssetGraphScrollViewer.Extent.Height
                - AssetGraphScrollViewer.Viewport.Height);

        AssetGraphScrollViewer.Offset = new Vector(
            Math.Clamp(_panStartOffset.X - delta.X, 0.0, maxOffsetX),
            Math.Clamp(_panStartOffset.Y - delta.Y, 0.0, maxOffsetY));
        e.Handled = true;
    }

    private void GraphPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (_connectionFromNode is not null)
        {
            var graphPosition = ToGraphPosition(e);
            if (DataContext is AssetGraphEditorPaneViewModel graph
                && graph.FindInputPortAt(graphPosition.X, graphPosition.Y) is { } inputPort)
            {
                graph.ConnectToInputPort(_connectionFromNode, inputPort);
                _connectionFromNode = null;
                _connectionFromPort = null;
                e.Handled = true;
                return;
            }

            CancelConnectionDrag();
            e.Handled = true;

            return;
        }

        if (_isPanning)
        {
            var wasClick = !_didRightDrag;
            FinishPanning(e.Pointer);
            if (wasClick)
            {
                ShowGraphContextMenu();
            }

            e.Handled = true;
            return;
        }

        if (!_isBoxSelecting)
        {
            return;
        }

        FinishBoxSelection(e);
        e.Handled = true;
    }

    private void FinishPanning(IPointer pointer)
    {
        _isPanning = false;
        pointer.Capture(null);
    }

    private void UpdateBoxSelectionDrag(PointerEventArgs e)
    {
        if (!_isBoxSelecting
            || e.Pointer.Captured != this
            || DataContext is not AssetGraphEditorPaneViewModel)
        {
            return;
        }

        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed)
        {
            FinishBoxSelection(e);
            return;
        }

        _boxSelectCurrentGraphPosition = ToGraphPosition(e);
        UpdateSelectionRectangle();
        e.Handled = true;
    }

    private void FinishBoxSelection(PointerEventArgs e)
    {
        if (DataContext is AssetGraphEditorPaneViewModel graph)
        {
            _boxSelectCurrentGraphPosition = ToGraphPosition(e);
            graph.SelectNodesInRectangle(
                _boxSelectStartGraphPosition.X,
                _boxSelectStartGraphPosition.Y,
                _boxSelectCurrentGraphPosition.X,
                _boxSelectCurrentGraphPosition.Y,
                e.KeyModifiers.HasFlag(KeyModifiers.Shift));
        }

        _isBoxSelecting = false;
        SelectionRectangle.IsVisible = false;
        e.Pointer.Capture(null);
    }

    private bool BeginConnectionDragFromEvent(PointerEventArgs e)
    {
        var graphPosition = ToGraphPosition(e);
        if (_connectionFromNode is not null
            || DataContext is not AssetGraphEditorPaneViewModel graph
            || graph.FindOutputPortAt(graphPosition.X, graphPosition.Y) is not { } port)
        {
            return false;
        }

        _connectionFromNode = port.Owner;
        _connectionFromPort = port;
        graph.BeginConnectionPreview(port.Owner, port);
        graph.UpdateConnectionPreviewEnd(graphPosition.X, graphPosition.Y);
        e.Handled = true;
        return true;
    }

    private void CancelConnectionDrag()
    {
        if (DataContext is AssetGraphEditorPaneViewModel graph)
        {
            graph.CancelConnectionPreview();
        }

        _connectionFromNode = null;
        _connectionFromPort = null;
    }

    private void UpdateSelectionRectangle()
    {
        var left = Math.Min(
            _boxSelectStartGraphPosition.X,
            _boxSelectCurrentGraphPosition.X);
        var top = Math.Min(
            _boxSelectStartGraphPosition.Y,
            _boxSelectCurrentGraphPosition.Y);
        var width = Math.Abs(
            _boxSelectCurrentGraphPosition.X
                - _boxSelectStartGraphPosition.X);
        var height = Math.Abs(
            _boxSelectCurrentGraphPosition.Y
                - _boxSelectStartGraphPosition.Y);

        SelectionRectangle.Margin = new Thickness(left, top, 0, 0);
        SelectionRectangle.Width = width;
        SelectionRectangle.Height = height;
        SelectionRectangle.IsVisible = true;
    }

    private void GraphPointerWheelChanged(object? sender, PointerWheelEventArgs e)
    {
        if (DataContext is not AssetGraphEditorPaneViewModel graph
            || !IsPointerInsideGraphViewport(e))
        {
            return;
        }

        var oldZoom = graph.Zoom;
        var cursor = e.GetPosition(AssetGraphScrollViewer);
        var oldOffset = AssetGraphScrollViewer.Offset;
        var graphPointUnderCursor = new Point(
            (oldOffset.X + cursor.X) / oldZoom,
            (oldOffset.Y + cursor.Y) / oldZoom);

        graph.ZoomByWheelDelta(e.Delta.Y);
        if (Math.Abs(graph.Zoom - oldZoom) > 0.0001)
        {
            SetScrollOffsetForGraphPoint(graph, graphPointUnderCursor, cursor);
            RequestZoomPersist();
        }

        e.Handled = true;
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        PersistPendingZoom();
        base.OnDetachedFromVisualTree(e);
    }

    private Point ToGraphPosition(PointerEventArgs e)
    {
        return e.GetPosition(AssetGraphCanvas);
    }

    private bool IsPointerInsideGraphViewport(PointerEventArgs e)
    {
        var point = e.GetPosition(AssetGraphScrollViewer);
        return point.X >= 0.0
            && point.Y >= 0.0
            && point.X <= AssetGraphScrollViewer.Bounds.Width
            && point.Y <= AssetGraphScrollViewer.Bounds.Height;
    }

    private static AssetGraphNodeCardViewModel? NodeUnderPointer(object? source)
    {
        var current = source as StyledElement;
        while (current is not null)
        {
            if (current.DataContext is AssetGraphNodeCardViewModel node)
            {
                return node;
            }

            current = current.Parent;
        }

        return null;
    }

    private static AssetGraphPortViewModel? PortUnderPointer(object? source)
    {
        var current = source as StyledElement;
        while (current is not null)
        {
            if (current.DataContext is AssetGraphPortViewModel port)
            {
                return port;
            }

            current = current.Parent;
        }

        return null;
    }

    private static AssetGraphSubGraph? SubGraphUnderPointer(object? source)
    {
        var current = source as StyledElement;
        while (current is not null)
        {
            if (current.DataContext is AssetGraphSubGraph proxy)
            {
                return proxy;
            }

            current = current.Parent;
        }

        return null;
    }

    // Right-click (without a pan drag) context menu — the primary surface for graph
    // operations, so nothing depends on keyboard shortcuts (which can be remapped or
    // intercepted by drivers). Content depends on what was under the pointer at press
    // time: a proxy offers Ungroup; a node/selection offers Collapse + Delete.
    private void ShowGraphContextMenu()
    {
        if (DataContext is not AssetGraphEditorPaneViewModel graph)
        {
            return;
        }

        var items = new List<Control>();

        if (PortUnderPointer(_rightDownSource) is { IsOutput: true } outputPort)
        {
            var owner = outputPort.Owner;
            items.Add(graph.IsReroute(owner.Id)
                ? BuildMenuItem("Remove named reroute", () => graph.RemoveReroute(owner))
                : BuildMenuItem("Create named reroute", () => graph.CreateReroute(owner)));
        }
        else if (SubGraphUnderPointer(_rightDownSource) is { } proxy)
        {
            graph.SelectSubGraph(proxy);
            items.Add(BuildMenuItem("Open sub-graph", () => graph.OpenSubGraph(proxy)));
            items.Add(BuildMenuItem("Ungroup", () => graph.Ungroup(proxy)));
        }
        else
        {
            if (NodeUnderPointer(_rightDownSource) is { } node && !node.IsSelected)
            {
                graph.SelectNode(node);
            }

            if (graph.SelectedNodes.Count > 0)
            {
                items.Add(BuildMenuItem(
                    "Collapse into sub-graph",
                    () => graph.CreateSubGraphFromSelection("Sub-graph")));
                items.Add(BuildMenuItem(
                    "Delete",
                    () => graph.RemoveSelectedNodes()));
            }
        }

        if (items.Count == 0)
        {
            return;
        }

        new MenuFlyout { ItemsSource = items }.ShowAt(this, showAtPointer: true);
    }

    private static MenuItem BuildMenuItem(string header, Action action)
    {
        var item = new MenuItem { Header = header };
        item.Click += (_, _) => action();
        return item;
    }

    private void SetScrollOffsetForGraphPoint(
        AssetGraphEditorPaneViewModel graph,
        Point graphPoint,
        Point viewportPoint)
    {
        var maxOffsetX = Math.Max(
            0.0,
            graph.ScaledGraphWidth - AssetGraphScrollViewer.Viewport.Width);
        var maxOffsetY = Math.Max(
            0.0,
            graph.ScaledGraphHeight - AssetGraphScrollViewer.Viewport.Height);

        AssetGraphScrollViewer.Offset = new Vector(
            Math.Clamp(graphPoint.X * graph.Zoom - viewportPoint.X, 0.0, maxOffsetX),
            Math.Clamp(graphPoint.Y * graph.Zoom - viewportPoint.Y, 0.0, maxOffsetY));
    }

    private void RequestZoomPersist()
    {
        _hasPendingZoomPersist = true;
        if (!_zoomPersistTimer.IsEnabled)
        {
            _zoomPersistTimer.Start();
        }
    }

    private void ZoomPersistTimerTick(object? sender, EventArgs e)
    {
        PersistPendingZoom();
    }

    private void PersistPendingZoom()
    {
        if (!_hasPendingZoomPersist)
        {
            _zoomPersistTimer.Stop();
            return;
        }

        _hasPendingZoomPersist = false;
        _zoomPersistTimer.Stop();
        if (DataContext is AssetGraphEditorPaneViewModel graph)
        {
            graph.CommitZoom();
        }
    }
}
