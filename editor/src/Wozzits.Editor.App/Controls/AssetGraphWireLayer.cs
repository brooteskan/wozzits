namespace Wozzits.Editor.App.Controls;

using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Wozzits.Editor.ViewModels.EditorPanes;

public sealed class AssetGraphWireLayer : Control
{
    public static readonly StyledProperty<IEnumerable?> EdgesProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, IEnumerable?>(
            nameof(Edges));

    public static readonly StyledProperty<IBrush?> StrokeProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, IBrush?>(
            nameof(Stroke));

    public static readonly StyledProperty<double> StrokeThicknessProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, double>(
            nameof(StrokeThickness),
            2.0);

    public static readonly StyledProperty<bool> IsPreviewVisibleProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, bool>(
            nameof(IsPreviewVisible));

    public static readonly StyledProperty<bool> IsPreviewRejectedProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, bool>(
            nameof(IsPreviewRejected));

    public static readonly StyledProperty<double> PreviewStartXProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, double>(
            nameof(PreviewStartX));

    public static readonly StyledProperty<double> PreviewStartYProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, double>(
            nameof(PreviewStartY));

    public static readonly StyledProperty<double> PreviewEndXProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, double>(
            nameof(PreviewEndX));

    public static readonly StyledProperty<double> PreviewEndYProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, double>(
            nameof(PreviewEndY));

    public static readonly StyledProperty<IBrush?> PreviewStrokeProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, IBrush?>(
            nameof(PreviewStroke));

    public static readonly StyledProperty<IBrush?> PreviewRejectedStrokeProperty =
        AvaloniaProperty.Register<AssetGraphWireLayer, IBrush?>(
            nameof(PreviewRejectedStroke));

    private readonly List<INotifyPropertyChanged> _observedEdges = [];
    private INotifyCollectionChanged? _observedCollection;

    public IEnumerable? Edges
    {
        get => GetValue(EdgesProperty);
        set => SetValue(EdgesProperty, value);
    }

    public IBrush? Stroke
    {
        get => GetValue(StrokeProperty);
        set => SetValue(StrokeProperty, value);
    }

    public double StrokeThickness
    {
        get => GetValue(StrokeThicknessProperty);
        set => SetValue(StrokeThicknessProperty, value);
    }

    public bool IsPreviewVisible
    {
        get => GetValue(IsPreviewVisibleProperty);
        set => SetValue(IsPreviewVisibleProperty, value);
    }

    public bool IsPreviewRejected
    {
        get => GetValue(IsPreviewRejectedProperty);
        set => SetValue(IsPreviewRejectedProperty, value);
    }

    public double PreviewStartX
    {
        get => GetValue(PreviewStartXProperty);
        set => SetValue(PreviewStartXProperty, value);
    }

    public double PreviewStartY
    {
        get => GetValue(PreviewStartYProperty);
        set => SetValue(PreviewStartYProperty, value);
    }

    public double PreviewEndX
    {
        get => GetValue(PreviewEndXProperty);
        set => SetValue(PreviewEndXProperty, value);
    }

    public double PreviewEndY
    {
        get => GetValue(PreviewEndYProperty);
        set => SetValue(PreviewEndYProperty, value);
    }

    public IBrush? PreviewStroke
    {
        get => GetValue(PreviewStrokeProperty);
        set => SetValue(PreviewStrokeProperty, value);
    }

    public IBrush? PreviewRejectedStroke
    {
        get => GetValue(PreviewRejectedStrokeProperty);
        set => SetValue(PreviewRejectedStrokeProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        if (change.Property == EdgesProperty)
        {
            ObserveEdges(change.NewValue as IEnumerable);
            InvalidateVisual();
            return;
        }

        if (change.Property == StrokeProperty
            || change.Property == StrokeThicknessProperty
            || change.Property == IsPreviewVisibleProperty
            || change.Property == IsPreviewRejectedProperty
            || change.Property == PreviewStartXProperty
            || change.Property == PreviewStartYProperty
            || change.Property == PreviewEndXProperty
            || change.Property == PreviewEndYProperty
            || change.Property == PreviewStrokeProperty
            || change.Property == PreviewRejectedStrokeProperty)
        {
            InvalidateVisual();
        }
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);

        var stroke = Stroke ?? Brushes.DodgerBlue;
        var pen = new Pen(stroke, StrokeThickness);
        if (Edges is not null)
        {
            foreach (var item in Edges)
            {
                if (item is not AssetGraphEdgeViewModel edge || edge.IsRenderHidden)
                {
                    continue;
                }

                DrawWire(context, pen, edge.StartX, edge.StartY, edge.EndX, edge.EndY);
            }
        }

        if (IsPreviewVisible)
        {
            var previewStroke = IsPreviewRejected
                ? PreviewRejectedStroke ?? Brushes.IndianRed
                : PreviewStroke ?? stroke;
            DrawWire(
                context,
                new Pen(previewStroke, StrokeThickness + 0.5),
                PreviewStartX,
                PreviewStartY,
                PreviewEndX,
                PreviewEndY);
        }
    }

    public AssetGraphEdgeViewModel? HitTestEdge(Point point, double tolerance)
    {
        if (Edges is null)
        {
            return null;
        }

        List<AssetGraphEdgeViewModel> edges = [];
        foreach (var item in Edges)
        {
            if (item is AssetGraphEdgeViewModel edge && !edge.IsRenderHidden)
            {
                edges.Add(edge);
            }
        }

        for (var index = edges.Count - 1; index >= 0; --index)
        {
            var edge = edges[index];
            if (DistanceToWire(point, edge) <= tolerance)
            {
                return edge;
            }
        }

        return null;
    }

    private static void DrawWire(
        DrawingContext context,
        Pen pen,
        double startX,
        double startY,
        double endX,
        double endY)
    {
        var tangent = Math.Max(48.0, Math.Abs(endX - startX) * 0.42);
        var geometry = new StreamGeometry();
        using (var stream = geometry.Open())
        {
            stream.BeginFigure(new Point(startX, startY), isFilled: false);
            stream.CubicBezierTo(
                new Point(startX + tangent, startY),
                new Point(endX - tangent, endY),
                new Point(endX, endY));
            stream.EndFigure(isClosed: false);
        }

        context.DrawGeometry(brush: null, pen, geometry);
    }

    private static double DistanceToWire(Point point, AssetGraphEdgeViewModel edge)
    {
        var tangent = Math.Max(48.0, Math.Abs(edge.EndX - edge.StartX) * 0.42);
        var previous = new Point(edge.StartX, edge.StartY);
        var best = double.PositiveInfinity;

        for (var step = 1; step <= 18; ++step)
        {
            var t = step / 18.0;
            var current = Cubic(
                t,
                new Point(edge.StartX, edge.StartY),
                new Point(edge.StartX + tangent, edge.StartY),
                new Point(edge.EndX - tangent, edge.EndY),
                new Point(edge.EndX, edge.EndY));
            best = Math.Min(best, DistanceToSegment(point, previous, current));
            previous = current;
        }

        return best;
    }

    private static Point Cubic(
        double t,
        Point p0,
        Point p1,
        Point p2,
        Point p3)
    {
        var u = 1.0 - t;
        var tt = t * t;
        var uu = u * u;
        var uuu = uu * u;
        var ttt = tt * t;

        return new Point(
            uuu * p0.X
                + 3.0 * uu * t * p1.X
                + 3.0 * u * tt * p2.X
                + ttt * p3.X,
            uuu * p0.Y
                + 3.0 * uu * t * p1.Y
                + 3.0 * u * tt * p2.Y
                + ttt * p3.Y);
    }

    private static double DistanceToSegment(Point point, Point a, Point b)
    {
        var segment = b - a;
        var lengthSquared = segment.X * segment.X + segment.Y * segment.Y;
        if (lengthSquared <= 0.0001)
        {
            return Distance(point, a);
        }

        var pointVector = point - a;
        var t = Math.Clamp(
            (pointVector.X * segment.X + pointVector.Y * segment.Y)
                / lengthSquared,
            0.0,
            1.0);
        return Distance(point, new Point(
            a.X + segment.X * t,
            a.Y + segment.Y * t));
    }

    private static double Distance(Point a, Point b)
    {
        var dx = a.X - b.X;
        var dy = a.Y - b.Y;
        return Math.Sqrt(dx * dx + dy * dy);
    }

    private void ObserveEdges(IEnumerable? edges)
    {
        if (_observedCollection is not null)
        {
            _observedCollection.CollectionChanged -= EdgesCollectionChanged;
            _observedCollection = null;
        }

        ClearObservedEdges();

        if (edges is INotifyCollectionChanged collection)
        {
            _observedCollection = collection;
            _observedCollection.CollectionChanged += EdgesCollectionChanged;
        }

        ObserveEdgeItems(edges);
    }

    private void ObserveEdgeItems(IEnumerable? edges)
    {
        if (edges is null)
        {
            return;
        }

        foreach (var item in edges)
        {
            if (item is not INotifyPropertyChanged edge)
            {
                continue;
            }

            edge.PropertyChanged += EdgePropertyChanged;
            _observedEdges.Add(edge);
        }
    }

    private void ClearObservedEdges()
    {
        foreach (var edge in _observedEdges)
        {
            edge.PropertyChanged -= EdgePropertyChanged;
        }

        _observedEdges.Clear();
    }

    private void EdgesCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        ClearObservedEdges();
        ObserveEdgeItems(Edges);
        InvalidateVisual();
    }

    private void EdgePropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        InvalidateVisual();
    }
}
