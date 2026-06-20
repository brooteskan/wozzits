using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.App.Views.EditorPanes;

public partial class AssetGraphEditorPaneView : UserControl
{
    private static readonly TimeSpan ZoomPersistInterval = TimeSpan.FromSeconds(15);
    private readonly DispatcherTimer _zoomPersistTimer;
    private AssetGraphNodeCardViewModel? _dragNode;
    private Control? _dragControl;
    private Avalonia.Point _lastPointerPosition;
    private bool _isPanning;
    private Avalonia.Point _panStartPointerPosition;
    private Avalonia.Vector _panStartOffset;
    private bool _isBoxSelecting;
    private Avalonia.Point _boxSelectStartGraphPosition;
    private Avalonia.Point _boxSelectCurrentGraphPosition;
    private bool _hasPendingZoomPersist;

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
        _lastPointerPosition = point.Position;
        e.Pointer.Capture(control);
        e.Handled = true;
    }

    private void NodeCardPointerMoved(object? sender, PointerEventArgs e)
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

        var current = point.Position;
        graph.MoveSelectedNodesByScreenDelta(
            _dragNode,
            current.X - _lastPointerPosition.X,
            current.Y - _lastPointerPosition.Y);
        _lastPointerPosition = current;
        e.Handled = true;
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
        e.Handled = true;
    }

    private void GraphPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        var point = e.GetCurrentPoint(this);
        if (point.Properties.IsRightButtonPressed)
        {
            _isPanning = true;
            _panStartPointerPosition = point.Position;
            _panStartOffset = AssetGraphScrollViewer.Offset;
            e.Pointer.Capture(this);
            e.Handled = true;
            return;
        }

        if (!point.Properties.IsLeftButtonPressed
            || DataContext is not AssetGraphEditorPaneViewModel graph
            || IsPointerInsideNodeCard(e.Source))
        {
            return;
        }

        _isBoxSelecting = true;
        _boxSelectStartGraphPosition = ToGraphPosition(e);
        _boxSelectCurrentGraphPosition = _boxSelectStartGraphPosition;
        UpdateSelectionRectangle();
        e.Pointer.Capture(this);
        e.Handled = true;
    }

    private void GraphPointerMoved(object? sender, PointerEventArgs e)
    {
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
        if (_isPanning)
        {
            FinishPanning(e.Pointer);
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
        if (DataContext is not AssetGraphEditorPaneViewModel graph)
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

    private static bool IsPointerInsideNodeCard(object? source)
    {
        var current = source as StyledElement;
        while (current is not null)
        {
            if (current.DataContext is AssetGraphNodeCardViewModel)
            {
                return true;
            }

            current = current.Parent;
        }

        return false;
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
