using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.Tests;

public sealed class AssetGraphGroupingTests
{
    // --- Pure grouping model -----------------------------------------------------------

    [Fact]
    public void CreateSubGraphGroupsSelectedMembers()
    {
        var model = new AssetGraphGroupingModel();

        var group = model.CreateSubGraph("Terrain", [1ul, 2ul, 3ul]);

        Assert.Same(group, Assert.Single(model.SubGraphs));
        Assert.Equal("Terrain", group.Name);
        Assert.Null(group.ParentId);
        Assert.Equal<ulong>([1, 2, 3], Members(group));
        Assert.Same(group, model.SubGraphOfNode(2));
        Assert.Null(model.SubGraphOfNode(9));
    }

    [Fact]
    public void CreateSubGraphDeduplicatesMembers()
    {
        var model = new AssetGraphGroupingModel();

        var group = model.CreateSubGraph("G", [5ul, 5ul, 6ul]);

        Assert.Equal<ulong>([5, 6], Members(group));
    }

    [Fact]
    public void CreateSubGraphWithNoMembersThrows()
    {
        var model = new AssetGraphGroupingModel();

        Assert.Throws<ArgumentException>(() => model.CreateSubGraph("Empty", []));
    }

    [Fact]
    public void CreatingSubGraphMovesNodeOutOfPriorGroup()
    {
        var model = new AssetGraphGroupingModel();
        var first = model.CreateSubGraph("First", [1ul, 2ul]);

        var second = model.CreateSubGraph("Second", [2ul, 3ul]);

        // Node 2 moves from 'first' into 'second' — a node lives in exactly one sub-graph.
        Assert.Equal<ulong>([1], Members(first));
        Assert.Equal<ulong>([2, 3], Members(second));
        Assert.Same(second, model.SubGraphOfNode(2));
        Assert.Equal(2, model.SubGraphs.Count);
    }

    [Fact]
    public void EmptyingAPriorGroupRemovesIt()
    {
        var model = new AssetGraphGroupingModel();
        var solo = model.CreateSubGraph("Solo", [7ul]);

        var next = model.CreateSubGraph("Next", [7ul]);

        // 'solo' lost its only node to 'next' and is dropped.
        Assert.Same(next, Assert.Single(model.SubGraphs));
        Assert.DoesNotContain(solo, model.SubGraphs);
    }

    [Fact]
    public void AddNodesMovesNodesIntoTarget()
    {
        var model = new AssetGraphGroupingModel();
        var target = model.CreateSubGraph("Target", [1ul]);
        var other = model.CreateSubGraph("Other", [2ul]);

        model.AddNodes(target.Id, [2ul]);

        Assert.Equal<ulong>([1, 2], Members(target));
        Assert.DoesNotContain(other, model.SubGraphs);
    }

    [Fact]
    public void RemoveNodeUngroupsAndDropsEmptyGroup()
    {
        var model = new AssetGraphGroupingModel();
        var group = model.CreateSubGraph("G", [1ul, 2ul]);

        model.RemoveNode(1);
        Assert.Equal<ulong>([2], Members(group));
        Assert.Null(model.SubGraphOfNode(1));

        model.RemoveNode(2);
        Assert.Empty(model.SubGraphs);
    }

    [Fact]
    public void UngroupDissolvesGroupAndUngroupsMembers()
    {
        var model = new AssetGraphGroupingModel();
        var group = model.CreateSubGraph("G", [1ul, 2ul]);

        Assert.True(model.Ungroup(group.Id));

        Assert.Empty(model.SubGraphs);
        Assert.Null(model.SubGraphOfNode(1));
        Assert.Null(model.SubGraphOfNode(2));
    }

    [Fact]
    public void UngroupUnknownIdReturnsFalse()
    {
        var model = new AssetGraphGroupingModel();

        Assert.False(model.Ungroup("does-not-exist"));
    }

    [Fact]
    public void UngroupReparentsNestedChildrenToGrandparent()
    {
        var model = new AssetGraphGroupingModel();
        var parent = model.CreateSubGraph("Parent", [1ul]);
        var child = model.CreateSubGraph("Child", [2ul], parentId: parent.Id);

        Assert.Equal(parent.Id, child.ParentId);

        model.Ungroup(parent.Id);

        // Child keeps its own member and reparents to root (parent's parent, i.e. null).
        Assert.Same(child, Assert.Single(model.SubGraphs));
        Assert.Null(child.ParentId);
    }

    [Fact]
    public void ReconcileDropsDeadMembersAndEmptyGroups()
    {
        var model = new AssetGraphGroupingModel();
        var terrain = model.CreateSubGraph("Terrain", [1ul, 2ul]);
        var sky = model.CreateSubGraph("Sky", [3ul]);

        model.ReconcileWithLiveNodes(new HashSet<ulong> { 1, 3 });
        Assert.Equal<ulong>([1], Members(terrain));
        Assert.Equal<ulong>([3], Members(sky));

        model.ReconcileWithLiveNodes(new HashSet<ulong> { 3 });
        Assert.DoesNotContain(terrain, model.SubGraphs);
        Assert.Same(sky, Assert.Single(model.SubGraphs));

        model.ReconcileWithLiveNodes(new HashSet<ulong>());
        Assert.Empty(model.SubGraphs);
    }

    [Fact]
    public void ClearRemovesEverything()
    {
        var model = new AssetGraphGroupingModel();
        model.CreateSubGraph("A", [1ul]);
        model.CreateSubGraph("B", [2ul]);

        model.Clear();

        Assert.Empty(model.SubGraphs);
        Assert.Null(model.SubGraphOfNode(1));
    }

    // --- Pane view-model wiring --------------------------------------------------------

    [Fact]
    public void CreateSubGraphFromSelectionGroupsSelectedNodesWithoutMarkingDraft()
    {
        var pane = new AssetGraphEditorPaneViewModel();
        pane.LoadSnapshot(Snapshot(1, 2, 3));

        pane.SelectNode(pane.Nodes[0]);
        pane.ToggleNodeSelection(pane.Nodes[1]);
        var group = pane.CreateSubGraphFromSelection("Terrain");

        Assert.NotNull(group);
        Assert.Equal("Terrain", group!.Name);
        Assert.Equal<ulong>([1, 2], group.MemberNodeIds.OrderBy(id => id).ToArray());
        Assert.Same(group, Assert.Single(pane.SubGraphs));
        // Grouping is editor-only view-state, not an engine edit — no draft/commit needed.
        Assert.False(pane.IsDraftGraph);
    }

    [Fact]
    public void CreateSubGraphFromEmptySelectionReportsError()
    {
        var pane = new AssetGraphEditorPaneViewModel();
        pane.LoadSnapshot(Snapshot(1));

        var group = pane.CreateSubGraphFromSelection("Nothing");

        Assert.Null(group);
        Assert.Empty(pane.SubGraphs);
        Assert.True(pane.HasLastEditError);
    }

    [Fact]
    public void ReloadingSnapshotReconcilesGroupMembershipAgainstLiveNodes()
    {
        var pane = new AssetGraphEditorPaneViewModel();
        pane.LoadSnapshot(Snapshot(1, 2, 3));
        pane.SelectNode(pane.Nodes[0]);          // node 1
        pane.ToggleNodeSelection(pane.Nodes[1]); // node 2
        var group = pane.CreateSubGraphFromSelection("Pair");
        Assert.Equal(2, group!.MemberNodeIds.Count);

        // Node 2 leaves the graph (e.g. deleted); the group keeps only live members.
        pane.LoadSnapshot(Snapshot(1, 3));
        Assert.Equal<ulong>([1], group.MemberNodeIds.OrderBy(id => id).ToArray());
        Assert.Same(group, Assert.Single(pane.SubGraphs));

        // Its last member disappears → the group is removed.
        pane.LoadSnapshot(Snapshot(3));
        Assert.Empty(pane.SubGraphs);
    }

    private static ulong[] Members(AssetGraphSubGraph subGraph) =>
        subGraph.MemberNodeIds.OrderBy(id => id).ToArray();

    private static EngineAssetGraphSnapshotResponse Snapshot(params ulong[] nodeIds) =>
        new()
        {
            Ok = true,
            Snapshot = new EngineAssetGraphSnapshot
            {
                Nodes = nodeIds
                    .Select(id => new EngineAssetGraphNode
                    {
                        Id = id,
                        TypeName = "Node",
                        DisplayName = $"node {id}",
                    })
                    .ToList(),
            },
        };
}
