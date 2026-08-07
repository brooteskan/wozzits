namespace Wozzits.Editor.App.Controls;

using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.ComponentModel;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using Wozzits.Editor.ViewModels.EditorPanes.Minds;

// Renders mind bonds as straight lines between qubit centres. A bond is an undirected
// coupling, so colour encodes the sign of j (ferromagnetic = agree, anti = disagree) and
// thickness encodes |j| (coupling strength). Observes the bond collection + each bond's
// endpoints so the layer repaints when a qubit is dragged or the graph is reprojected.
public sealed class MindBondLayer : Control
{
    public static readonly StyledProperty<IEnumerable?> EdgesProperty =
        AvaloniaProperty.Register<MindBondLayer, IEnumerable?>(nameof(Edges));

    public static readonly StyledProperty<IBrush?> FerroBrushProperty =
        AvaloniaProperty.Register<MindBondLayer, IBrush?>(nameof(FerroBrush));

    public static readonly StyledProperty<IBrush?> AntiBrushProperty =
        AvaloniaProperty.Register<MindBondLayer, IBrush?>(nameof(AntiBrush));

    private readonly List<INotifyPropertyChanged> _observed = [];
    private INotifyCollectionChanged? _observedCollection;

    public IEnumerable? Edges
    {
        get => GetValue(EdgesProperty);
        set => SetValue(EdgesProperty, value);
    }

    public IBrush? FerroBrush
    {
        get => GetValue(FerroBrushProperty);
        set => SetValue(FerroBrushProperty, value);
    }

    public IBrush? AntiBrush
    {
        get => GetValue(AntiBrushProperty);
        set => SetValue(AntiBrushProperty, value);
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);

        if (change.Property == EdgesProperty)
        {
            Observe(change.NewValue as IEnumerable);
            InvalidateVisual();
        }
        else if (change.Property == FerroBrushProperty || change.Property == AntiBrushProperty)
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

        var ferro = FerroBrush ?? Brushes.MediumSeaGreen;
        var anti = AntiBrush ?? Brushes.IndianRed;

        foreach (var item in Edges)
        {
            if (item is not MindBondViewModel bond)
            {
                continue;
            }

            var brush = bond.IsFerromagnetic ? ferro : anti;
            if (bond.IsDimmed)
            {
                brush = Dim(brush);
            }

            double thickness = 1.5 + Math.Min(4.0, Math.Abs(bond.J) * 3.0);
            var pen = new Pen(brush, thickness) { LineCap = PenLineCap.Round };
            context.DrawLine(pen, new Point(bond.StartX, bond.StartY), new Point(bond.EndX, bond.EndY));
        }
    }

    private static IBrush Dim(IBrush brush) => brush is ISolidColorBrush s
        ? new SolidColorBrush(Color.FromArgb((byte)(s.Color.A * 0.3), s.Color.R, s.Color.G, s.Color.B))
        : brush;

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
            if (item is INotifyPropertyChanged bond)
            {
                bond.PropertyChanged += OnItemChanged;
                _observed.Add(bond);
            }
        }
    }

    private void ClearItems()
    {
        foreach (var bond in _observed)
        {
            bond.PropertyChanged -= OnItemChanged;
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
