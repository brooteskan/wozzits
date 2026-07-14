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

    [Fact]
    public void Add_Qubit_Appends_And_Selects()
    {
        var mind = new Mind();
        mind.Qubits.Add(new MindQubit { Id = "q0" });
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        var node = pane.AddQubit();

        Assert.NotNull(node);
        Assert.Equal(2, mind.Qubits.Count);
        Assert.Equal(2, pane.Nodes.Count);
        Assert.Same(node, pane.SelectedNode);
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Add_Bond_Rejects_Self_And_Duplicate()
    {
        var pane = new MindPaneViewModel();
        pane.Project(Sample());   // already has q0<->q1 and q0<->q2

        Assert.False(pane.AddBond("q0", "q0", 1));   // self-bond
        Assert.False(pane.AddBond("q0", "q1", 1));   // duplicate pair
        Assert.False(pane.AddBond("q1", "q0", 1));   // duplicate, other direction
        Assert.True(pane.AddBond("q1", "q2", 0.4));  // a genuinely new pair

        Assert.Equal(3, pane.Bonds.Count);
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Remove_Bond_Drops_It()
    {
        var mind = Sample();
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        pane.RemoveBond(mind.Bonds[0]);

        Assert.Single(mind.Bonds);
        Assert.Single(pane.Bonds);
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Goal_Editor_Updates_Model_And_Marks_Dirty()
    {
        var mind = Sample();
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        pane.Nodes[1].GoalEditor.Value = "0.75";

        Assert.Equal(0.75, mind.Qubits[1].Goal);
        Assert.Equal("+0.75", pane.Nodes[1].GoalLabel);
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Connect_Target_Adds_A_Bond_And_Keeps_The_Selection()
    {
        var mind = new Mind();
        mind.Qubits.Add(new MindQubit { Id = "q0" });
        mind.Qubits.Add(new MindQubit { Id = "q1" });
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        pane.SelectOnly(pane.Nodes[0]);
        Assert.Single(pane.ConnectTargets);          // only q1 is connectable

        pane.ConnectTarget = pane.ConnectTargets[0]; // connect q0 <-> q1

        Assert.Single(mind.Bonds);
        Assert.NotNull(pane.SelectedNode);
        Assert.Equal("q0", pane.SelectedNode!.NodeId); // selection kept
        Assert.Single(pane.SelectedNodeBonds);
        Assert.Empty(pane.ConnectTargets);            // q1 now already bonded
    }

    [Fact]
    public void Bond_J_Editor_Updates_The_Model_And_Sign()
    {
        var pane = new MindPaneViewModel();
        pane.Project(Sample());
        var bond = pane.Bonds[0];   // q0<->q1, j = -0.8 (anti)
        Assert.False(bond.IsFerromagnetic);

        bond.JEditor.Value = "0.5";

        Assert.Equal(0.5, bond.Model.J);
        Assert.True(bond.IsFerromagnetic);
        Assert.True(pane.IsDirty);
    }

    [Fact]
    public void Bond_Remove_Command_Drops_It()
    {
        var mind = Sample();
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        pane.Bonds[0].RemoveCommand.Execute(null);

        Assert.Single(mind.Bonds);
    }

    [Fact]
    public void Validation_Flags_A_Non_Chain_Ttn()
    {
        var mind = new Mind { Chi = 2 };
        mind.Qubits.Add(new MindQubit { Id = "q0" });
        mind.Qubits.Add(new MindQubit { Id = "q1" });
        mind.Qubits.Add(new MindQubit { Id = "q2" });
        mind.Bonds.Add(new MindBond { A = "q0", B = "q2", J = 1 }); // skips q1 -> not a chain
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        Assert.True(pane.HasValidationWarning);
        Assert.Contains("chain", pane.ValidationWarning);
    }
}
