namespace Wozzits.Editor.App.Controls;

using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// Renders dataflow wires as left-to-right cubic beziers (same look as the asset-graph
// canvas). Observes the wire collection and each wire's endpoints so the layer repaints
// when a node is dragged or the graph is reprojected.
public sealed class DataflowWireLayer : Control
{
    public static readonly StyledProperty<IEnumerable?> EdgesProperty =
        AvaloniaProperty.Register<DataflowWireLayer, IEnumerable?>(nameof(Edges));

    public static readonly StyledProperty<IBrush?> StrokeProperty =
        AvaloniaProperty.Register<DataflowWireLayer, IBrush?>(nameof(Stroke));

    public static readonly StyledProperty<double> StrokeThicknessProperty =
        AvaloniaProperty.Register<DataflowWireLayer, double>(nameof(StrokeThickness), 2.0);

    private readonly List<INotifyPropertyChanged> _observed = [];
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
            Observe(change.NewValue as IEnumerable);
            InvalidateVisual();
        }
        else if (change.Property == StrokeProperty || change.Property == StrokeThicknessProperty)
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
        var dimPen = stroke is ISolidColorBrush solid
            ? new Pen(
                new SolidColorBrush(Color.FromArgb((byte)(solid.Color.A * 0.28), solid.Color.R, solid.Color.G, solid.Color.B)),
                StrokeThickness)
            : pen;

        foreach (var item in Edges)
        {
            if (item is not DataflowWireViewModel wire)
            {
                continue;
            }

            DrawWire(context, wire.IsDimmed ? dimPen : pen, wire.StartX, wire.StartY, wire.EndX, wire.EndY);
        }
    }

    private static void DrawWire(DrawingContext context, Pen pen, double sx, double sy, double ex, double ey)
    {
        var tangent = Math.Max(48.0, Math.Abs(ex - sx) * 0.42);
        var geometry = new StreamGeometry();
        using (var stream = geometry.Open())
        {
            stream.BeginFigure(new Point(sx, sy), isFilled: false);
            stream.CubicBezierTo(
                new Point(sx + tangent, sy),
                new Point(ex - tangent, ey),
                new Point(ex, ey));
            stream.EndFigure(isClosed: false);
        }

        context.DrawGeometry(brush: null, pen, geometry);
    }

    private void Observe(IEnumerable? edges)
    {
        if (_observedCollection is not null)
        {
            _observedCollection.CollectionChanged -= OnCollectionChanged;
            _observedCollection = null;
        }

        ClearItems();

        if (edges is INotifyCollectionChanged collection)
        {
            _observedCollection = collection;
            _observedCollection.CollectionChanged += OnCollectionChanged;
        }

        ObserveItems(edges);
    }

    private void ObserveItems(IEnumerable? edges)
    {
        if (edges is null)
        {
            return;
        }

        foreach (var item in edges)
        {
            if (item is INotifyPropertyChanged wire)
            {
                wire.PropertyChanged += OnItemChanged;
                _observed.Add(wire);
            }
        }
    }

    private void ClearItems()
    {
        foreach (var wire in _observed)
        {
            wire.PropertyChanged -= OnItemChanged;
        }

        _observed.Clear();
    }

    private void OnCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        ClearItems();
        ObserveItems(Edges);
        InvalidateVisual();
    }

    private void OnItemChanged(object? sender, PropertyChangedEventArgs e) => InvalidateVisual();
}
