namespace Wozzits.Editor.App.Controls;

using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// Shared canvas interaction for the statechart panes (dataflow + control): right-drag pans,
// wheel zooms about the cursor, left-drag on empty space marquee-selects, left-drag on a node
// moves the selection. Mirrors the asset-graph canvas but drives any IEditorCanvas, so the
// plumbing lives once. Node hit-testing walks the visual tree for an ICanvasNode DataContext,
// so the node templates need no per-card handlers.
public sealed class GraphInteraction
{
    private readonly Control _root;
    private readonly ScrollViewer _scrollViewer;
    private readonly Control _canvas;
    private readonly Border _selectionRectangle;
    private readonly Func<IEditorCanvas?> _canvasVm;

    private ICanvasNode? _dragNode;
    private Point _lastGraphPosition;
    private bool _isPanning;
    private Point _panStartPointer;
    private Vector _panStartOffset;
    private bool _isBoxSelecting;
    private Point _boxStart;
    private Point _boxCurrent;

    public GraphInteraction(
        Control root,
        ScrollViewer scrollViewer,
        Control canvas,
        Border selectionRectangle,
        Func<IEditorCanvas?> canvasVm)
    {
        _root = root;
        _scrollViewer = scrollViewer;
        _canvas = canvas;
        _selectionRectangle = selectionRectangle;
        _canvasVm = canvasVm;

        root.AddHandler(InputElement.PointerPressedEvent, OnPressed, RoutingStrategies.Tunnel, handledEventsToo: true);
        root.AddHandler(InputElement.PointerMovedEvent, OnMoved, RoutingStrategies.Tunnel, handledEventsToo: true);
        root.AddHandler(InputElement.PointerReleasedEvent, OnReleased, RoutingStrategies.Tunnel, handledEventsToo: true);
        root.AddHandler(InputElement.PointerWheelChangedEvent, OnWheel, RoutingStrategies.Tunnel, handledEventsToo: true);
        root.Focusable = true;
    }

    private Point ToGraph(PointerEventArgs e) => e.GetPosition(_canvas);

    private void OnPressed(object? sender, PointerPressedEventArgs e)
    {
        var vm = _canvasVm();
        if (vm is null)
        {
            return;
        }

        var point = e.GetCurrentPoint(_root);

        if (point.Properties.IsRightButtonPressed)
        {
            _isPanning = true;
            _panStartPointer = point.Position;
            _panStartOffset = _scrollViewer.Offset;
            e.Pointer.Capture(_root);
            e.Handled = true;
            return;
        }

        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }

        var node = NodeUnder(e.Source);
        if (node is not null)
        {
            if (e.KeyModifiers.HasFlag(KeyModifiers.Shift))
            {
                vm.ToggleSelection(node);
            }
            else if (!node.IsSelected)
            {
                vm.SelectOnly(node);
            }

            if (!node.IsSelected)
            {
                e.Handled = true;
                return;
            }

            _dragNode = node;
            _lastGraphPosition = ToGraph(e);
            _root.Focus();
            e.Pointer.Capture(_root);
            e.Handled = true;
            return;
        }

        _isBoxSelecting = true;
        _boxStart = ToGraph(e);
        _boxCurrent = _boxStart;
        UpdateSelectionRectangle();
        _root.Focus();
        e.Pointer.Capture(_root);
        e.Handled = true;
    }

    private void OnMoved(object? sender, PointerEventArgs e)
    {
        var vm = _canvasVm();
        if (vm is null)
        {
            return;
        }

        if (_dragNode is not null && e.Pointer.Captured == _root)
        {
            var point = e.GetCurrentPoint(_root);
            if (!point.Properties.IsLeftButtonPressed)
            {
                EndDrag(e.Pointer);
                return;
            }

            var current = ToGraph(e);
            vm.MoveSelectedBy(current.X - _lastGraphPosition.X, current.Y - _lastGraphPosition.Y);
            _lastGraphPosition = current;
            e.Handled = true;
            return;
        }

        if (_isPanning && e.Pointer.Captured == _root)
        {
            var point = e.GetCurrentPoint(_root);
            if (!point.Properties.IsRightButtonPressed)
            {
                EndPan(e.Pointer);
                return;
            }

            var delta = point.Position - _panStartPointer;
            double maxX = Math.Max(0.0, _scrollViewer.Extent.Width - _scrollViewer.Viewport.Width);
            double maxY = Math.Max(0.0, _scrollViewer.Extent.Height - _scrollViewer.Viewport.Height);
            _scrollViewer.Offset = new Vector(
                Math.Clamp(_panStartOffset.X - delta.X, 0.0, maxX),
                Math.Clamp(_panStartOffset.Y - delta.Y, 0.0, maxY));
            e.Handled = true;
            return;
        }

        if (_isBoxSelecting && e.Pointer.Captured == _root)
        {
            var point = e.GetCurrentPoint(_root);
            if (!point.Properties.IsLeftButtonPressed)
            {
                EndBoxSelection(vm, e);
                return;
            }

            _boxCurrent = ToGraph(e);
            UpdateSelectionRectangle();
            e.Handled = true;
        }
    }

    private void OnReleased(object? sender, PointerReleasedEventArgs e)
    {
        if (_dragNode is not null)
        {
            EndDrag(e.Pointer);
            e.Handled = true;
            return;
        }

        if (_isPanning)
        {
            EndPan(e.Pointer);
            e.Handled = true;
            return;
        }

        if (_isBoxSelecting && _canvasVm() is { } vm)
        {
            EndBoxSelection(vm, e);
            e.Handled = true;
        }
    }

    private void OnWheel(object? sender, PointerWheelEventArgs e)
    {
        var vm = _canvasVm();
        if (vm is null)
        {
            return;
        }

        double oldZoom = vm.Zoom;
        var cursor = e.GetPosition(_scrollViewer);
        var oldOffset = _scrollViewer.Offset;
        double graphX = (oldOffset.X + cursor.X) / oldZoom;
        double graphY = (oldOffset.Y + cursor.Y) / oldZoom;

        vm.ZoomByWheel(e.Delta.Y);

        if (Math.Abs(vm.Zoom - oldZoom) > 0.0001)
        {
            double maxX = Math.Max(0.0, vm.ScaledGraphWidth - _scrollViewer.Viewport.Width);
            double maxY = Math.Max(0.0, vm.ScaledGraphHeight - _scrollViewer.Viewport.Height);
            _scrollViewer.Offset = new Vector(
                Math.Clamp(graphX * vm.Zoom - cursor.X, 0.0, maxX),
                Math.Clamp(graphY * vm.Zoom - cursor.Y, 0.0, maxY));
        }

        e.Handled = true;
    }

    private void EndDrag(IPointer pointer)
    {
        _dragNode = null;
        pointer.Capture(null);
    }

    private void EndPan(IPointer pointer)
    {
        _isPanning = false;
        pointer.Capture(null);
    }

    private void EndBoxSelection(IEditorCanvas vm, PointerEventArgs e)
    {
        _boxCurrent = ToGraph(e);
        vm.SelectInRectangle(
            _boxStart.X, _boxStart.Y, _boxCurrent.X, _boxCurrent.Y,
            e.KeyModifiers.HasFlag(KeyModifiers.Shift));
        _isBoxSelecting = false;
        _selectionRectangle.IsVisible = false;
        e.Pointer.Capture(null);
    }

    private void UpdateSelectionRectangle()
    {
        double left = Math.Min(_boxStart.X, _boxCurrent.X);
        double top = Math.Min(_boxStart.Y, _boxCurrent.Y);
        _selectionRectangle.Margin = new Thickness(left, top, 0, 0);
        _selectionRectangle.Width = Math.Abs(_boxCurrent.X - _boxStart.X);
        _selectionRectangle.Height = Math.Abs(_boxCurrent.Y - _boxStart.Y);
        _selectionRectangle.IsVisible = true;
    }

    private static ICanvasNode? NodeUnder(object? source)
    {
        var current = source as StyledElement;
        while (current is not null)
        {
            if (current.DataContext is ICanvasNode node)
            {
                return node;
            }

            current = current.Parent;
        }

        return null;
    }
}
