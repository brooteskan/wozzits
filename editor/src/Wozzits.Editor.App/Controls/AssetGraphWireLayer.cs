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
            || change.Property == StrokeThicknessProperty)
        {
            InvalidateVisual();
        }
    }

    public override void Render(DrawingContext context)
    {
        base.Render(context);

        if (Edges is null)
        {
            return;
        }

        var stroke = Stroke ?? Brushes.DodgerBlue;
        var pen = new Pen(stroke, StrokeThickness);
        foreach (var item in Edges)
        {
            if (item is not AssetGraphEdgeViewModel edge)
            {
                continue;
            }

            DrawWire(context, pen, edge.StartX, edge.StartY, edge.EndX, edge.EndY);
        }
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
