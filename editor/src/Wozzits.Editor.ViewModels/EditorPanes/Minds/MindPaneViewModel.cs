namespace Wozzits.Editor.ViewModels.EditorPanes.Minds;

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using CommunityToolkit.Mvvm.Input;
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

    public MindPaneViewModel()
    {
        AddQubitCommand = new RelayCommand(() => AddQubit());
        DeleteSelectedCommand = new RelayCommand(DeleteSelected);
    }

    public IRelayCommand AddQubitCommand { get; }

    public IRelayCommand DeleteSelectedCommand { get; }

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
                OnPropertyChanged(nameof(SelectedNodeBonds));
                OnPropertyChanged(nameof(ConnectTargets));
            }
        }
    }

    public bool HasSelectedNode => _selectedNode is not null;

    // The bonds incident to the selected qubit (the properties panel's bond list).
    public IReadOnlyList<MindBondViewModel> SelectedNodeBonds =>
        _selectedNode is null
            ? Array.Empty<MindBondViewModel>()
            : Bonds.Where(b => b.A == _selectedNode || b.B == _selectedNode).ToList();

    // Qubits the selected one could be coupled to (not itself, not already bonded).
    public IReadOnlyList<MindNodeViewModel> ConnectTargets =>
        _selectedNode is null
            ? Array.Empty<MindNodeViewModel>()
            : Nodes.Where(n => n != _selectedNode && !AlreadyBonded(_selectedNode, n)).ToList();

    // Set by the panel's "connect to" picker: couples the selected qubit to the chosen one
    // (default ferromagnetic j = 1), keeping the selection so the panel stays put. Resets to
    // null immediately -- it is a one-shot trigger, not a sticky value.
    private MindNodeViewModel? _connectTarget;

    public MindNodeViewModel? ConnectTarget
    {
        get => _connectTarget;
        set
        {
            _connectTarget = null;
            OnPropertyChanged();
            if (value is not null && _selectedNode is { } sel && AddBond(sel.NodeId, value.NodeId, 1.0))
            {
                var again = Nodes.FirstOrDefault(n => n.NodeId == sel.NodeId);
                if (again is not null)
                {
                    SelectOnly(again);
                }
            }
        }
    }

    private bool AlreadyBonded(MindNodeViewModel a, MindNodeViewModel b) =>
        Bonds.Any(x => (x.A == a && x.B == b) || (x.A == b && x.B == a));

    // Advisory (empty when fine): does the drawn graph fit the chosen backend? chi>=2 TTN
    // needs a nearest-neighbour chain; chi=0 exact grows exponentially past a handful.
    public string ValidationWarning
    {
        get
        {
            if (_mind is null)
            {
                return string.Empty;
            }

            if (_mind.Chi >= 2)
            {
                foreach (var b in _mind.Bonds)
                {
                    if (Math.Abs(_mind.IndexOfQubit(b.A) - _mind.IndexOfQubit(b.B)) != 1)
                    {
                        return "TTN (chi ≥ 2) needs a nearest-neighbour chain — some bonds skip qubits.";
                    }
                }
            }
            else if (_mind.Chi == 0 && _mind.Qubits.Count > 12)
            {
                return $"exact backend on {_mind.Qubits.Count} qubits is exponential — consider chi 1 (loopy BP).";
            }

            return string.Empty;
        }
    }

    public bool HasValidationWarning => ValidationWarning.Length > 0;

    // Re-evaluate the advisory after a global param (e.g. chi) changes on the document side.
    public void NotifyParamsChanged()
    {
        OnPropertyChanged(nameof(ValidationWarning));
        OnPropertyChanged(nameof(HasValidationWarning));
    }

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

    // ---- structural authoring -------------------------------------------------

    // Append a qubit, place it at a clear spot below the others, and select it.
    public MindNodeViewModel? AddQubit()
    {
        if (_mind is null)
        {
            return null;
        }

        var id = FreshQubitId();
        _mind.Qubits.Add(new MindQubit { Id = id });
        IsDirty = true;
        ReprojectPreservingLayout();

        var node = Nodes.FirstOrDefault(n => n.NodeId == id);
        if (node is not null)
        {
            double bottom = CanvasPadding;
            foreach (var n in Nodes)
            {
                if (n != node)
                {
                    bottom = Math.Max(bottom, n.Y + MindNodeViewModel.NodeHeight);
                }
            }

            node.X = CanvasPadding;
            node.Y = bottom + 40.0;
            SelectOnly(node);
            RaiseExtentChanged();
        }

        return node;
    }

    // Add an undirected coupling between two distinct qubits (by id). Rejects a self-bond
    // or a duplicate of an existing pair (either direction). Default j is set by the caller.
    public bool AddBond(string aId, string bId, double j)
    {
        if (_mind is null || aId == bId
            || _mind.Qubits.All(q => q.Id != aId)
            || _mind.Qubits.All(q => q.Id != bId)
            || _mind.Bonds.Any(b => (b.A == aId && b.B == bId) || (b.A == bId && b.B == aId)))
        {
            return false;
        }

        _mind.Bonds.Add(new MindBond { A = aId, B = bId, J = j });
        IsDirty = true;
        ReprojectPreservingLayout();
        return true;
    }

    public void RemoveBond(MindBond bond)
    {
        if (_mind is null)
        {
            return;
        }

        if (_mind.Bonds.Remove(bond))
        {
            IsDirty = true;
            ReprojectPreservingLayout();
        }
    }

    // The lowest unused "qN" id. Display titles are positional (q0..) regardless; this id is
    // only the edit-safe handle bonds reference within a session.
    private string FreshQubitId()
    {
        var used = _mind!.Qubits.Select(q => q.Id).ToHashSet();
        for (int i = 0; ; i++)
        {
            var id = "q" + i;
            if (!used.Contains(id))
            {
                return id;
            }
        }
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
            var node = new MindNodeViewModel(mind.Qubits[i], i) { GoalEdited = MarkDirty };
            Nodes.Add(node);
            byId[node.NodeId] = node;
        }

        LayOutCircular();

        foreach (var bond in mind.Bonds)
        {
            if (byId.TryGetValue(bond.A, out var a) && byId.TryGetValue(bond.B, out var b))
            {
                Bonds.Add(new MindBondViewModel(a, b, bond)
                {
                    JEdited = MarkDirty,
                    RemoveRequested = () => RemoveBond(bond),
                });
            }
        }

        OnPropertyChanged(nameof(HasMind));
        OnPropertyChanged(nameof(HasGraph));
        OnPropertyChanged(nameof(ValidationWarning));
        RaiseExtentChanged();
    }

    // Snapshot the hand-placed positions + zoom for the .mind.editor.json sidecar.
    public MindLayout CaptureLayout()
    {
        var layout = new MindLayout { Zoom = Zoom };
        foreach (var n in Nodes)
        {
            layout.Positions[n.NodeId] = new MindLayout.Point(n.X, n.Y);
        }

        return layout;
    }

    // Restore saved positions + zoom over the auto-layout (a restore -- does not mark dirty).
    public void ApplyLayout(MindLayout layout)
    {
        Zoom = layout.Zoom;
        foreach (var n in Nodes)
        {
            if (layout.Positions.TryGetValue(n.NodeId, out var p))
            {
                n.X = p.X;
                n.Y = p.Y;
            }
        }

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
        var selectedIds = SelectedNodes.Select(n => n.NodeId).ToHashSet();
        double zoom = Zoom;
        Project(_mind);
        foreach (var n in Nodes)
        {
            if (positions.TryGetValue(n.NodeId, out var p))
            {
                n.X = p.X;
                n.Y = p.Y;
            }

            if (selectedIds.Contains(n.NodeId))
            {
                n.IsSelected = true;
                SelectedNodes.Add(n);
            }
        }

        SelectedNode = SelectedNodes.LastOrDefault();
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
