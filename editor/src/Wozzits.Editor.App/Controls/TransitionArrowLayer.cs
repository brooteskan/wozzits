namespace Wozzits.Editor.App.Controls;

using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Globalization;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// Renders control-layer transitions as arrows between state boxes: a bowed cubic bezier
// clipped to the box borders with an arrowhead, or a loop above the box for a self-directed
// transition. Trigger labels are drawn near each arrow's apex. The bow means the two arrows
// of a back-and-forth pair (A->B, B->A) separate instead of overlapping.
public sealed class TransitionArrowLayer : Control
{
    public static readonly StyledProperty<IEnumerable?> EdgesProperty =
        AvaloniaProperty.Register<TransitionArrowLayer, IEnumerable?>(nameof(Edges));

    public static readonly StyledProperty<IBrush?> StrokeProperty =
        AvaloniaProperty.Register<TransitionArrowLayer, IBrush?>(nameof(Stroke));

    public static readonly StyledProperty<IBrush?> LabelBrushProperty =
        AvaloniaProperty.Register<TransitionArrowLayer, IBrush?>(nameof(LabelBrush));

    public static readonly StyledProperty<double> StrokeThicknessProperty =
        AvaloniaProperty.Register<TransitionArrowLayer, double>(nameof(StrokeThickness), 2.0);

    public static readonly StyledProperty<double> BoxWidthProperty =
        AvaloniaProperty.Register<TransitionArrowLayer, double>(nameof(BoxWidth), 180.0);

    public static readonly StyledProperty<double> BoxHeightProperty =
        AvaloniaProperty.Register<TransitionArrowLayer, double>(nameof(BoxHeight), 76.0);

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

    public IBrush? LabelBrush
    {
        get => GetValue(LabelBrushProperty);
        set => SetValue(LabelBrushProperty, value);
    }

    public double StrokeThickness
    {
        get => GetValue(StrokeThicknessProperty);
        set => SetValue(StrokeThicknessProperty, value);
    }

    public double BoxWidth
    {
        get => GetValue(BoxWidthProperty);
        set => SetValue(BoxWidthProperty, value);
    }

    public double BoxHeight
    {
        get => GetValue(BoxHeightProperty);
        set => SetValue(BoxHeightProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        if (change.Property == EdgesProperty)
        {
            Observe(change.NewValue as IEnumerable);
            InvalidateVisual();
        }
        else if (change.Property == StrokeProperty
                 || change.Property == LabelBrushProperty
                 || change.Property == StrokeThicknessProperty
                 || change.Property == BoxWidthProperty
                 || change.Property == BoxHeightProperty)
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

        var stroke = Stroke ?? Brushes.Gray;
        var pen = new Pen(stroke, StrokeThickness);
        var labelBrush = LabelBrush ?? stroke;
        double hw = BoxWidth / 2.0;
        double hh = BoxHeight / 2.0;

        foreach (var item in Edges)
        {
            if (item is not TransitionViewModel t)
            {
                continue;
            }

            if (t.IsSelfLoop)
            {
                DrawSelfLoop(context, pen, stroke, labelBrush, t, hw, hh);
            }
            else
            {
                DrawArrow(context, pen, stroke, labelBrush, t, hw, hh);
            }
        }
    }

    private void DrawArrow(
        DrawingContext context, Pen pen, IBrush stroke, IBrush labelBrush,
        TransitionViewModel t, double hw, double hh)
    {
        var fromCentre = new Point(t.StartX, t.StartY);
        var toCentre = new Point(t.EndX, t.EndY);
        var p0 = BorderPoint(fromCentre, toCentre, hw, hh);
        var p3 = BorderPoint(toCentre, fromCentre, hw, hh);

        var dir = Normalize(p3 - p0);
        var perp = new Vector(-dir.Y, dir.X);
        // Lane fans same-direction parallels apart; opposite directions already separate because
        // perp flips with the arrow direction.
        double bow = 26.0 + t.Lane * 22.0;
        var c1 = Lerp(p0, p3, 0.33) + perp * bow;
        var c2 = Lerp(p0, p3, 0.66) + perp * bow;

        var geometry = new StreamGeometry();
        using (var stream = geometry.Open())
        {
            stream.BeginFigure(p0, isFilled: false);
            stream.CubicBezierTo(c1, c2, p3);
            stream.EndFigure(isClosed: false);
        }

        context.DrawGeometry(brush: null, pen, geometry);
        DrawArrowHead(context, stroke, c2, p3);
        DrawLabel(context, labelBrush, t.Label, Lerp(p0, p3, 0.5) + perp * (bow + 9.0));
    }

    private void DrawSelfLoop(
        DrawingContext context, Pen pen, IBrush stroke, IBrush labelBrush,
        TransitionViewModel t, double hw, double hh)
    {
        // A loop arching above the box, arrowhead re-entering the top edge.
        double cx = t.StartX;
        double topY = t.StartY - hh;
        var start = new Point(cx - hw * 0.35, topY);
        var end = new Point(cx + hw * 0.35, topY);
        double height = 46.0 + t.Lane * 26.0;   // stack multiple self-loops at rising heights
        var c1 = new Point(cx - hw * 0.55, topY - height);
        var c2 = new Point(cx + hw * 0.55, topY - height);

        var geometry = new StreamGeometry();
        using (var stream = geometry.Open())
        {
            stream.BeginFigure(start, isFilled: false);
            stream.CubicBezierTo(c1, c2, end);
            stream.EndFigure(isClosed: false);
        }

        context.DrawGeometry(brush: null, pen, geometry);
        DrawArrowHead(context, stroke, c2, end);
        DrawLabel(context, labelBrush, t.Label, new Point(cx, topY - height - 9.0));
    }

    private static void DrawArrowHead(DrawingContext context, IBrush brush, Point from, Point tip)
    {
        var dir = Normalize(tip - from);
        if (dir.X == 0 && dir.Y == 0)
        {
            return;
        }

        var perp = new Vector(-dir.Y, dir.X);
        const double length = 10.0;
        const double halfWidth = 5.5;
        var basePoint = tip - dir * length;

        var geometry = new StreamGeometry();
        using (var stream = geometry.Open())
        {
            stream.BeginFigure(tip, isFilled: true);
            stream.LineTo(basePoint + perp * halfWidth);
            stream.LineTo(basePoint - perp * halfWidth);
            stream.EndFigure(isClosed: true);
        }

        context.DrawGeometry(brush, pen: null, geometry);
    }

    private static void DrawLabel(DrawingContext context, IBrush brush, string text, Point centre)
    {
        if (string.IsNullOrEmpty(text))
        {
            return;
        }

        var formatted = new FormattedText(
            text,
            CultureInfo.CurrentCulture,
            FlowDirection.LeftToRight,
            Typeface.Default,
            11.0,
            brush);
        context.DrawText(formatted, new Point(centre.X - formatted.Width / 2.0, centre.Y - formatted.Height / 2.0));
    }

    // The point where the centre-to-centre line exits an axis-aligned box (half extents hw,hh).
    private static Point BorderPoint(Point centre, Point toward, double hw, double hh)
    {
        double dx = toward.X - centre.X;
        double dy = toward.Y - centre.Y;
        if (dx == 0 && dy == 0)
        {
            return centre;
        }

        double sx = dx == 0 ? double.PositiveInfinity : hw / Math.Abs(dx);
        double sy = dy == 0 ? double.PositiveInfinity : hh / Math.Abs(dy);
        double s = Math.Min(sx, sy);
        return new Point(centre.X + dx * s, centre.Y + dy * s);
    }

    private static Vector Normalize(Vector v)
    {
        double length = Math.Sqrt(v.X * v.X + v.Y * v.Y);
        return length < 1e-6 ? default : v / length;
    }

    private static Point Lerp(Point a, Point b, double t) =>
        new(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t);

    private void Observe(IEnumerable? transitions)
    {
        if (_observedCollection is not null)
        {
            _observedCollection.CollectionChanged -= OnCollectionChanged;
            _observedCollection = null;
        }

        ClearItems();

        if (transitions is INotifyCollectionChanged collection)
        {
            _observedCollection = collection;
            _observedCollection.CollectionChanged += OnCollectionChanged;
        }

        ObserveItems(transitions);
    }

    private void ObserveItems(IEnumerable? transitions)
    {
        if (transitions is null)
        {
            return;
        }

        foreach (var item in transitions)
        {
            if (item is INotifyPropertyChanged transition)
            {
                transition.PropertyChanged += OnItemChanged;
                _observed.Add(transition);
            }
        }
    }

    private void ClearItems()
    {
        foreach (var transition in _observed)
        {
            transition.PropertyChanged -= OnItemChanged;
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
