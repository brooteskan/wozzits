namespace Wozzits.Editor.ViewModels.EditorPanes.Minds;

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// The mind graph canvas: qubit NODES + bond EDGES projected from a Mind. Implements the
// shared IEditorCanvas so the interaction controller drives pan / zoom / marquee-select /
// node-drag for free. This seam is projection + navigation + delete; structural authoring
// (add qubit, draw/delete bond, edit goal) layers on in 2c.
public sealed class MindPaneViewModel : ViewModelBase, IEditorCanvas
{
    private const double CanvasPadding = 28.0;
    private const double MinZoom = 0.25;
    private const double MaxZoom = 4.0;

    private Mind? _mind;
    private double _zoom = 1.0;
    private bool _isDirty;
    private bool _isLayoutDirty;
    private MindNodeViewModel? _selectedNode;

    public ObservableCollection<MindNodeViewModel> Nodes { get; } = [];

    public ObservableCollection<MindBondViewModel> Bonds { get; } = [];

    public ObservableCollection<MindNodeViewModel> SelectedNodes { get; } = [];

    public bool HasMind => _mind is not null;

    public bool HasGraph => Nodes.Count > 0;

    public MindNodeViewModel? SelectedNode
    {
        get => _selectedNode;
        private set
        {
            if (SetProperty(ref _selectedNode, value))
            {
                OnPropertyChanged(nameof(HasSelectedNode));
            }
        }
    }

    public bool HasSelectedNode => _selectedNode is not null;

    public bool IsDirty { get => _isDirty; private set => SetProperty(ref _isDirty, value); }

    public bool IsLayoutDirty { get => _isLayoutDirty; private set => SetProperty(ref _isLayoutDirty, value); }

    public void MarkDirty() => IsDirty = true;

    public void ClearDirty()
    {
        IsDirty = false;
        IsLayoutDirty = false;
    }

    // ---- IEditorCanvas --------------------------------------------------------

    public double Zoom
    {
        get => _zoom;
        set
        {
            if (SetProperty(ref _zoom, Math.Clamp(value, MinZoom, MaxZoom)))
            {
                OnPropertyChanged(nameof(ScaledGraphWidth));
                OnPropertyChanged(nameof(ScaledGraphHeight));
            }
        }
    }

    public double GraphWidth =>
        Nodes.Count == 0 ? 0 : Nodes.Max(n => n.X + MindNodeViewModel.NodeWidth) + CanvasPadding;

    public double GraphHeight =>
        Nodes.Count == 0 ? 0 : Nodes.Max(n => n.Y + MindNodeViewModel.NodeHeight) + CanvasPadding;

    public double ScaledGraphWidth => GraphWidth * Zoom;

    public double ScaledGraphHeight => GraphHeight * Zoom;

    public IReadOnlyList<ICanvasNode> CanvasNodes => Nodes;

    public void SelectOnly(ICanvasNode node)
    {
        foreach (var n in Nodes)
        {
            n.IsSelected = false;
        }

        SelectedNodes.Clear();
        if (node is MindNodeViewModel m)
        {
            m.IsSelected = true;
            SelectedNodes.Add(m);
            SelectedNode = m;
        }
        else
        {
            SelectedNode = null;
        }
    }

    public void ToggleSelection(ICanvasNode node)
    {
        if (node is not MindNodeViewModel m)
        {
            return;
        }

        m.IsSelected = !m.IsSelected;
        if (m.IsSelected)
        {
            if (!SelectedNodes.Contains(m))
            {
                SelectedNodes.Add(m);
            }

            SelectedNode = m;
        }
        else
        {
            SelectedNodes.Remove(m);
            SelectedNode = SelectedNodes.LastOrDefault();
        }
    }

    public void ClearSelection()
    {
        foreach (var n in Nodes)
        {
            n.IsSelected = false;
        }

        SelectedNodes.Clear();
        SelectedNode = null;
    }

    public void SelectInRectangle(double x0, double y0, double x1, double y1, bool additive)
    {
        double left = Math.Min(x0, x1);
        double right = Math.Max(x0, x1);
        double top = Math.Min(y0, y1);
        double bottom = Math.Max(y0, y1);

        if (!additive)
        {
            ClearSelection();
        }

        foreach (var n in Nodes)
        {
            bool overlaps = n.X < right && n.X + MindNodeViewModel.NodeWidth > left
                && n.Y < bottom && n.Y + MindNodeViewModel.NodeHeight > top;
            if (overlaps && !n.IsSelected)
            {
                n.IsSelected = true;
                SelectedNodes.Add(n);
            }
        }

        SelectedNode = SelectedNodes.LastOrDefault();
    }

    public void MoveSelectedBy(double dx, double dy)
    {
        foreach (var n in SelectedNodes)
        {
            n.X = Math.Max(0, n.X + dx);
            n.Y = Math.Max(0, n.Y + dy);
        }

        RaiseExtentChanged();
        IsLayoutDirty = true;
    }

    public void ZoomByWheel(double wheelDelta)
    {
        Zoom = wheelDelta > 0 ? Zoom * 1.1 : Zoom / 1.1;
        IsLayoutDirty = true;
    }

    public void DeleteSelected()
    {
        if (_mind is null || SelectedNodes.Count == 0)
        {
            return;
        }

        var ids = SelectedNodes.Select(n => n.NodeId).ToHashSet();
        _mind.Qubits.RemoveAll(q => ids.Contains(q.Id));
        _mind.Bonds.RemoveAll(b => ids.Contains(b.A) || ids.Contains(b.B));
        IsDirty = true;
        ReprojectPreservingLayout();
    }

    // ---- projection -----------------------------------------------------------

    public void Project(Mind mind)
    {
        _mind = mind;
        foreach (var b in Bonds)
        {
            b.Dispose();
        }

        Bonds.Clear();
        Nodes.Clear();
        SelectedNodes.Clear();
        SelectedNode = null;

        var byId = new Dictionary<string, MindNodeViewModel>();
        for (int i = 0; i < mind.Qubits.Count; i++)
        {
            var node = new MindNodeViewModel(mind.Qubits[i], i);
            Nodes.Add(node);
            byId[node.NodeId] = node;
        }

        LayOutCircular();

        foreach (var bond in mind.Bonds)
        {
            if (byId.TryGetValue(bond.A, out var a) && byId.TryGetValue(bond.B, out var b))
            {
                Bonds.Add(new MindBondViewModel(a, b, bond));
            }
        }

        OnPropertyChanged(nameof(HasMind));
        OnPropertyChanged(nameof(HasGraph));
        RaiseExtentChanged();
    }

    // Rebuild the projection but keep each surviving qubit's hand-placed position.
    public void ReprojectPreservingLayout()
    {
        if (_mind is null)
        {
            return;
        }

        var positions = Nodes.ToDictionary(n => n.NodeId, n => (n.X, n.Y));
        double zoom = Zoom;
        Project(_mind);
        foreach (var n in Nodes)
        {
            if (positions.TryGetValue(n.NodeId, out var p))
            {
                n.X = p.X;
                n.Y = p.Y;
            }
        }

        Zoom = zoom;
        RaiseExtentChanged();
    }

    // Place qubits evenly on a circle. Bonds are undirected couplings, so there is no DAG
    // layering to follow -- a ring reads the graph's symmetry well and never overlaps.
    private void LayOutCircular()
    {
        int n = Nodes.Count;
        if (n == 0)
        {
            return;
        }

        if (n == 1)
        {
            Nodes[0].X = CanvasPadding;
            Nodes[0].Y = CanvasPadding;
            return;
        }

        double radius = Math.Max(70.0, n * 26.0);
        double cx = CanvasPadding + radius + MindNodeViewModel.NodeWidth / 2.0;
        double cy = CanvasPadding + radius + MindNodeViewModel.NodeHeight / 2.0;
        for (int i = 0; i < n; i++)
        {
            double angle = -Math.PI / 2.0 + 2.0 * Math.PI * i / n;
            Nodes[i].X = cx + radius * Math.Cos(angle) - MindNodeViewModel.NodeWidth / 2.0;
            Nodes[i].Y = cy + radius * Math.Sin(angle) - MindNodeViewModel.NodeHeight / 2.0;
        }
    }

    private void RaiseExtentChanged()
    {
        OnPropertyChanged(nameof(GraphWidth));
        OnPropertyChanged(nameof(GraphHeight));
        OnPropertyChanged(nameof(ScaledGraphWidth));
        OnPropertyChanged(nameof(ScaledGraphHeight));
    }
}
