namespace Wozzits.Editor.App.Controls;

using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// Shared canvas interaction for the statechart panes (dataflow + control): right-drag pans,
// wheel zooms about the cursor, left-drag on empty space marquee-selects, left-drag on a node
// moves the selection. Two authoring gestures ride on the same plumbing, gated by capability
// interfaces so each is inert on the other canvas: dragging an output->input port wires ops
// (IWiringCanvas, dataflow), and -- while the draw tool is armed -- dragging state->state
// authors a transition (ITransitionDrawCanvas, control). Both share one dashed preview line.
// Node hit-testing walks the visual tree for an ICanvasNode DataContext.
public sealed class GraphInteraction
{
    private readonly Control _root;
    private readonly ScrollViewer _scrollViewer;
    private readonly Control _canvas;
    private readonly Border _selectionRectangle;
    private readonly Line? _previewLine;
    private readonly Func<IEditorCanvas?> _canvasVm;

    private ICanvasNode? _dragNode;
    private Point _lastGraphPosition;
    private bool _isPanning;
    private Point _panStartPointer;
    private Vector _panStartOffset;
    private bool _isBoxSelecting;
    private Point _boxStart;
    private Point _boxCurrent;
    private DataflowPortViewModel? _wireSource;
    private StateNodeViewModel? _transitionSource;
    private Point _previewStart;

    public GraphInteraction(
        Control root,
        ScrollViewer scrollViewer,
        Control canvas,
        Border selectionRectangle,
        Func<IEditorCanvas?> canvasVm,
        Line? previewLine = null)
    {
        _root = root;
        _scrollViewer = scrollViewer;
        _canvas = canvas;
        _selectionRectangle = selectionRectangle;
        _previewLine = previewLine;
        _canvasVm = canvasVm;

        root.AddHandler(InputElement.PointerPressedEvent, OnPressed, RoutingStrategies.Tunnel, handledEventsToo: true);
        root.AddHandler(InputElement.PointerMovedEvent, OnMoved, RoutingStrategies.Tunnel, handledEventsToo: true);
        root.AddHandler(InputElement.PointerReleasedEvent, OnReleased, RoutingStrategies.Tunnel, handledEventsToo: true);
        root.AddHandler(InputElement.PointerWheelChangedEvent, OnWheel, RoutingStrategies.Tunnel, handledEventsToo: true);
        root.AddHandler(InputElement.KeyDownEvent, OnKeyDown, RoutingStrategies.Tunnel | RoutingStrategies.Bubble, handledEventsToo: true);
        root.Focusable = true;
    }

    private Point ToGraph(PointerEventArgs e) => e.GetPosition(_canvas);

    private void OnPressed(object? sender, PointerPressedEventArgs e)
    {
        var vm = _canvasVm();
        if (vm is null || !WithinCanvas(e.Source))
        {
            return;   // presses on pane chrome (toolbar) are not canvas gestures
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

        // Armed "draw transition" tool (control canvas): a drag from a state to another state
        // authors a transition; a press on empty space cancels the tool.
        if (vm is ITransitionDrawCanvas { IsDrawingTransition: true } drawVm)
        {
            if (NodeUnder(e.Source) is StateNodeViewModel fromState)
            {
                _transitionSource = fromState;
                _previewStart = new Point(
                    fromState.X + ControlPaneViewModel.StateWidth / 2,
                    fromState.Y + ControlPaneViewModel.StateHeight / 2);
                UpdatePreview(ToGraph(e));
                _root.Focus();
                e.Pointer.Capture(_root);
            }
            else
            {
                drawVm.DisarmTransitionDraw();
            }

            e.Handled = true;
            return;
        }

        // Grabbing a node's OUTPUT port starts a wire drag (dataflow only). Input ports and the
        // rest of the card fall through to node drag / marquee below.
        if (vm is IWiringCanvas && PortUnder(e.Source) is { IsOutput: true } outputPort)
        {
            _wireSource = outputPort;
            _previewStart = new Point(outputPort.AnchorX, outputPort.AnchorY);
            UpdatePreview(ToGraph(e));
            _root.Focus();
            e.Pointer.Capture(_root);
            e.Handled = true;
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

        if (_transitionSource is not null && e.Pointer.Captured == _root)
        {
            var point = e.GetCurrentPoint(_root);
            if (!point.Properties.IsLeftButtonPressed)
            {
                EndTransition(e, point.Position);
                return;
            }

            UpdatePreview(ToGraph(e));
            e.Handled = true;
            return;
        }

        if (_wireSource is not null && e.Pointer.Captured == _root)
        {
            var point = e.GetCurrentPoint(_root);
            if (!point.Properties.IsLeftButtonPressed)
            {
                EndWire(e, point.Position);
                return;
            }

            UpdatePreview(ToGraph(e));
            e.Handled = true;
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
        if (_transitionSource is not null)
        {
            EndTransition(e, e.GetPosition(_root));
            e.Handled = true;
            return;
        }

        if (_wireSource is not null)
        {
            EndWire(e, e.GetPosition(_root));
            e.Handled = true;
            return;
        }

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
        if (vm is null || !WithinCanvas(e.Source))
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

    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        if (e.Key is Key.Delete or Key.Back && _canvasVm() is { } vm)
        {
            vm.DeleteSelected();
            e.Handled = true;
        }
    }

    private void EndDrag(IPointer pointer)
    {
        _dragNode = null;
        pointer.Capture(null);
    }

    // Finish a wire drag: if released over an INPUT port, ask the canvas to connect the source
    // op's output to that operand (the VM rejects invalid/cyclic connects). Always hide the
    // preview + release capture.
    private void EndWire(PointerEventArgs e, Point rootPosition)
    {
        var source = _wireSource;
        _wireSource = null;
        HidePreview();
        e.Pointer.Capture(null);

        if (source is null || _canvasVm() is not IWiringCanvas wiring)
        {
            return;
        }

        if (PortAtPoint(rootPosition) is { IsInput: true } target)
        {
            wiring.TryConnect(source.Owner.NodeId, target.Owner.NodeId, target.Index);
        }
    }

    // Finish a transition draw: if released over a state, add source -> that state (self-drop =
    // self-loop). Always hide the preview, disarm the tool, release capture.
    private void EndTransition(PointerEventArgs e, Point rootPosition)
    {
        var source = _transitionSource;
        _transitionSource = null;
        HidePreview();
        e.Pointer.Capture(null);

        if (source is null || _canvasVm() is not ITransitionDrawCanvas drawVm)
        {
            return;
        }

        if (StateAtPoint(rootPosition) is { } target)
        {
            drawVm.TryAddTransition(source.StateId, target.StateId);
        }

        drawVm.DisarmTransitionDraw();
    }

    private void UpdatePreview(Point graphEnd)
    {
        if (_previewLine is null)
        {
            return;
        }

        _previewLine.StartPoint = _previewStart;
        _previewLine.EndPoint = graphEnd;
        _previewLine.IsVisible = true;
    }

    private void HidePreview()
    {
        if (_previewLine is not null)
        {
            _previewLine.IsVisible = false;
        }
    }

    private DataflowPortViewModel? PortAtPoint(Point rootPosition) =>
        PortUnder(_root.InputHitTest(rootPosition));

    private StateNodeViewModel? StateAtPoint(Point rootPosition) =>
        NodeUnder(_root.InputHitTest(rootPosition)) as StateNodeViewModel;

    private static DataflowPortViewModel? PortUnder(object? source)
    {
        var current = source as StyledElement;
        while (current is not null)
        {
            if (current.DataContext is DataflowPortViewModel port)
            {
                return port;
            }

            current = current.Parent;
        }

        return null;
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

    // True when the event originated inside the scrollable canvas (not the pane's toolbar/
    // chrome), so canvas gestures never fire from a button press.
    private bool WithinCanvas(object? source)
    {
        for (var current = source as Visual; current is not null; current = current.GetVisualParent())
        {
            if (ReferenceEquals(current, _scrollViewer))
            {
                return true;
            }
            if (ReferenceEquals(current, _root))
            {
                return false;
            }
        }

        return false;
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
