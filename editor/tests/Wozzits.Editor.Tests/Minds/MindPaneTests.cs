using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Minds;

namespace Wozzits.Editor.Tests.Minds;

/// <summary>The mind canvas projection: a Mind becomes qubit nodes + bond edges the
/// shared interaction controller can drive. Pins projection, edge-follows-drag, delete,
/// and layout-preserving reprojection.</summary>
public sealed class MindPaneTests
{
    // q0 <-> q1 (anti) and q0 <-> q2 (ferro); q0 is in both bonds.
    private static Mind Sample()
    {
        var m = new Mind();
        m.Qubits.Add(new MindQubit { Id = "q0", Goal = 0.4 });
        m.Qubits.Add(new MindQubit { Id = "q1" });
        m.Qubits.Add(new MindQubit { Id = "q2" });
        m.Bonds.Add(new MindBond { A = "q0", B = "q1", J = -0.8 });
        m.Bonds.Add(new MindBond { A = "q0", B = "q2", J = 0.5 });
        return m;
    }

    [Fact]
    public void Projects_Qubits_And_Bonds()
    {
        var pane = new MindPaneViewModel();
        pane.Project(Sample());

        Assert.Equal(3, pane.Nodes.Count);
        Assert.Equal(2, pane.Bonds.Count);
        Assert.Equal(3, pane.CanvasNodes.Count);
        Assert.All(pane.Nodes, n => Assert.True(n.X >= 0 && n.Y >= 0)); // laid out on the circle

        // the first bond joins q0 and q1; sign of j classifies the coupling.
        Assert.Equal(pane.Nodes[0], pane.Bonds[0].A);
        Assert.Equal(pane.Nodes[1], pane.Bonds[0].B);
        Assert.False(pane.Bonds[0].IsFerromagnetic);  // j = -0.8 (anti)
        Assert.True(pane.Bonds[1].IsFerromagnetic);   // j = 0.5 (ferro)
    }

    [Fact]
    public void Bond_Endpoints_Follow_A_Node_Drag()
    {
        var pane = new MindPaneViewModel();
        pane.Project(Sample());
        var bond = pane.Bonds[0];
        double startX0 = bond.StartX;

        pane.SelectOnly(pane.Nodes[0]);
        pane.MoveSelectedBy(50, 30);

        Assert.NotEqual(startX0, bond.StartX);            // the edge moved with its node
        Assert.Equal(pane.Nodes[0].CenterX, bond.StartX); // and stays on the node's centre
        Assert.True(pane.IsLayoutDirty);
    }

    [Fact]
    public void Delete_Removes_The_Qubit_And_Its_Bonds()
    {
        var mind = Sample();
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        pane.SelectOnly(pane.Nodes[0]);   // q0 -- in both bonds
        pane.DeleteSelected();

        Assert.Equal(2, mind.Qubits.Count);
        Assert.Empty(mind.Bonds);         // both bonds referenced q0
        Assert.Equal(2, pane.Nodes.Count);
        Assert.Empty(pane.Bonds);
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Reproject_Preserves_Hand_Placed_Positions()
    {
        var pane = new MindPaneViewModel();
        pane.Project(Sample());
        pane.Nodes[0].X = 123;
        pane.Nodes[0].Y = 45;

        pane.ReprojectPreservingLayout();

        Assert.Equal(123, pane.Nodes[0].X);
        Assert.Equal(45, pane.Nodes[0].Y);
    }
}
