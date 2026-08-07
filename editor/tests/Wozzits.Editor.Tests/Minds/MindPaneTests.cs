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

    // A non-chain chi >= 2 mind BUILDS (the engine routes it to the general graph tensor
    // network), so the advisory is about COST, not validity -- it must not tell the author
    // the mind needs a chain.
    [Fact]
    public void Validation_Notes_The_Cost_Of_A_Non_Chain_Ttn()
    {
        var mind = new Mind { Chi = 2 };
        mind.Qubits.Add(new MindQubit { Id = "q0" });
        mind.Qubits.Add(new MindQubit { Id = "q1" });
        mind.Qubits.Add(new MindQubit { Id = "q2" });
        mind.Bonds.Add(new MindBond { A = "q0", B = "q2", J = 1 }); // skips q1 -> not a chain
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        Assert.True(pane.HasValidationWarning);
        Assert.Contains("cheaper", pane.ValidationWarning);
        Assert.DoesNotContain("needs", pane.ValidationWarning);
    }

    // A ring at chi >= 2 is a legitimate authored mind (bounded entanglement on any
    // topology) -- the pane may note its cost but must not present it as broken.
    [Fact]
    public void Validation_Does_Not_Reject_A_Ring_Ttn()
    {
        var mind = new Mind { Chi = 2 };
        mind.Qubits.Add(new MindQubit { Id = "q0" });
        mind.Qubits.Add(new MindQubit { Id = "q1" });
        mind.Qubits.Add(new MindQubit { Id = "q2" });
        mind.Bonds.Add(new MindBond { A = "q0", B = "q1", J = 1 });
        mind.Bonds.Add(new MindBond { A = "q1", B = "q2", J = 1 });
        mind.Bonds.Add(new MindBond { A = "q2", B = "q0", J = 1 }); // closes the ring
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        Assert.DoesNotContain("needs", pane.ValidationWarning);
    }

    // ---- agents ---------------------------------------------------------------

    private static MindNodeViewModel Node(MindPaneViewModel pane, string id) =>
        pane.Nodes.First(n => n.NodeId == id);

    [Fact]
    public void Group_Command_Needs_A_Multi_Selection()
    {
        var pane = new MindPaneViewModel();
        pane.Project(Sample());

        pane.SelectOnly(pane.Nodes[0]);
        Assert.False(pane.CanGroupSelection);
        Assert.False(pane.GroupSelectedCommand.CanExecute(null));

        pane.ToggleSelection(pane.Nodes[1]);
        Assert.True(pane.CanGroupSelection);
        Assert.True(pane.GroupSelectedCommand.CanExecute(null));
    }

    [Fact]
    public void Group_Selected_Puts_Them_In_One_Agent()
    {
        var mind = Sample();   // q0, q1, q2
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        pane.SelectOnly(Node(pane, "q0"));
        pane.ToggleSelection(Node(pane, "q1"));
        pane.GroupSelected();

        // q0 and q1 share one agent (2 dispositions); q2 keeps its own.
        var shared = mind.AgentOf("q0")!;
        Assert.Equal(shared.Id, mind.AgentOf("q1")!.Id);
        Assert.Equal(2, mind.MembersOf(shared.Id).Count);
        Assert.NotEqual(shared.Id, mind.AgentOf("q2")!.Id);
        Assert.True(pane.IsDirty);

        // the grouped nodes carry a tint; the loner does not.
        Assert.True(Node(pane, "q0").IsGrouped);
        Assert.True(Node(pane, "q1").IsGrouped);
        Assert.False(Node(pane, "q2").IsGrouped);
    }

    [Fact]
    public void Group_Then_Isolate_Restores_Singletons()
    {
        var mind = Sample();
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        pane.SelectOnly(Node(pane, "q0"));
        pane.ToggleSelection(Node(pane, "q1"));
        pane.GroupSelected();
        Assert.True(Node(pane, "q0").IsGrouped);

        pane.SelectOnly(Node(pane, "q0"));
        pane.ToggleSelection(Node(pane, "q1"));
        pane.IsolateSelected();

        Assert.False(Node(pane, "q0").IsGrouped);
        Assert.False(Node(pane, "q1").IsGrouped);
        Assert.NotEqual(mind.AgentOf("q0")!.Id, mind.AgentOf("q1")!.Id);
        Assert.False(mind.HasLayout);
    }

    [Fact]
    public void Selected_Agent_One_Hot_Makes_It_Exclusive()
    {
        var mind = Sample();
        var pane = new MindPaneViewModel();
        pane.Project(mind);
        pane.SelectOnly(Node(pane, "q0"));
        pane.ToggleSelection(Node(pane, "q1"));
        pane.GroupSelected();

        pane.SelectOnly(Node(pane, "q0"));
        Assert.NotNull(pane.SelectedAgent);
        Assert.True(pane.HasSelectedAgentGroup);

        pane.SelectedAgent!.OneHotEditor.Value = "2";

        Assert.Equal(2.0, mind.AgentOf("q0")!.OneHot);
        Assert.True(Node(pane, "q0").IsExclusive);
        Assert.Equal("A0", Node(pane, "q0").GroupTag);
        Assert.True(pane.IsDirty);
    }

    // Grouping REORDERS the flat slot indices (each agent's dispositions go contiguous),
    // and the "qubit N" title tracks that slot -- so an actuator author sees the real slot.
    [Fact]
    public void Grouping_Renumbers_The_Flat_Slot_Titles()
    {
        var mind = Sample();   // q0, q1, q2
        var pane = new MindPaneViewModel();
        pane.Project(mind);

        pane.SelectOnly(Node(pane, "q0"));
        pane.ToggleSelection(Node(pane, "q2"));
        pane.GroupSelected();   // {q0, q2} become one agent's contiguous block

        Assert.Equal("qubit 0", Node(pane, "q0").Title);
        Assert.Equal("qubit 1", Node(pane, "q2").Title);   // a's 2nd disposition
        Assert.Equal("qubit 2", Node(pane, "q1").Title);   // pushed after the group
    }
}
