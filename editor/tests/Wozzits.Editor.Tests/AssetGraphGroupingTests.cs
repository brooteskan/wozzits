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

    // --- Canvas projection (proxy rendering) -------------------------------------------

    [Fact]
    public void ProjectionHidesGroupedNodesAndReroutesBoundaryEdges()
    {
        var pane = new AssetGraphEditorPaneViewModel();
        pane.LoadSnapshot(SnapshotWithEdges(
            [1, 2, 3, 4],
            (2, 1),   // becomes intra-group once (1,2) are grouped
            (1, 3),   // boundary: grouped 1 -> ungrouped 3
            (3, 4))); // both ungrouped

        pane.SelectNode(NodeById(pane, 1));
        pane.ToggleNodeSelection(NodeById(pane, 2));
        var group = pane.CreateSubGraphFromSelection("G");
        Assert.NotNull(group);

        Assert.False(NodeById(pane, 1).IsCanvasVisible);
        Assert.False(NodeById(pane, 2).IsCanvasVisible);
        Assert.True(NodeById(pane, 3).IsCanvasVisible);
        Assert.True(NodeById(pane, 4).IsCanvasVisible);

        var intra = EdgeBetween(pane, 2, 1);
        var boundary = EdgeBetween(pane, 1, 3);
        var outside = EdgeBetween(pane, 3, 4);

        Assert.True(intra.IsRenderHidden);
        Assert.False(boundary.IsRenderHidden);
        Assert.False(outside.IsRenderHidden);

        // Boundary edge starts at the proxy's output anchor, ends at node 3's card.
        Assert.Same(group, boundary.FromProxy);
        Assert.Null(boundary.ToProxy);
        Assert.Equal(group!.ProxyX + AssetGraphSubGraph.ProxyWidth, boundary.StartX);
        Assert.Equal(NodeById(pane, 3).X, boundary.EndX);

        // Ungrouping restores visibility and un-reroutes the edges.
        pane.SelectSubGraph(group);
        Assert.True(pane.UngroupSelectedSubGraph());
        Assert.True(NodeById(pane, 1).IsCanvasVisible);
        Assert.False(intra.IsRenderHidden);
        Assert.Null(boundary.FromProxy);
        Assert.Empty(pane.SubGraphs);
    }

    [Fact]
    public void ProjectionReroutesCrossGroupEdgeToBothProxies()
    {
        var pane = new AssetGraphEditorPaneViewModel();
        pane.LoadSnapshot(SnapshotWithEdges([1, 2], (1, 2)));

        pane.SelectNode(NodeById(pane, 1));
        var a = pane.CreateSubGraphFromSelection("A");
        pane.SelectNode(NodeById(pane, 2));
        var b = pane.CreateSubGraphFromSelection("B");

        var edge = EdgeBetween(pane, 1, 2);
        Assert.False(edge.IsRenderHidden);
        Assert.Same(a, edge.FromProxy);
        Assert.Same(b, edge.ToProxy);
        Assert.Equal(a!.ProxyX + AssetGraphSubGraph.ProxyWidth, edge.StartX);
        Assert.Equal(b!.ProxyX, edge.EndX);
    }

    [Fact]
    public void SelectingNodesClearsSubGraphSelectionAndViceVersa()
    {
        var pane = new AssetGraphEditorPaneViewModel();
        pane.LoadSnapshot(SnapshotWithEdges([1, 2]));

        pane.SelectNode(NodeById(pane, 1));
        var group = pane.CreateSubGraphFromSelection("G");
        // Creating a group selects its proxy and drops the node selection.
        Assert.Same(group, pane.SelectedSubGraph);
        Assert.Empty(pane.SelectedNodes);

        // Selecting a node clears the proxy selection (mutually exclusive).
        pane.SelectNode(NodeById(pane, 2));
        Assert.Null(pane.SelectedSubGraph);
        Assert.False(group!.IsSelected);
    }

    [Fact]
    public void DrillInContextShowsOnlyItsMembers()
    {
        var grouping = new AssetGraphGroupingModel();
        var root = new AssetGraphEditorPaneViewModel(grouping: grouping);
        root.LoadSnapshot(SnapshotWithEdges([1, 2, 3], (1, 2), (2, 3)));
        root.SelectNode(NodeById(root, 1));
        root.ToggleNodeSelection(NodeById(root, 2));
        var g = root.CreateSubGraphFromSelection("G");
        Assert.NotNull(g);

        // A drill-in pane over the SAME grouping, with context = G.
        var drill = new AssetGraphEditorPaneViewModel(grouping: grouping, context: g);
        drill.LoadSnapshot(SnapshotWithEdges([1, 2, 3], (1, 2), (2, 3)));

        // Inside G: only its members are visible; the outside node and its edge are hidden.
        Assert.True(NodeById(drill, 1).IsCanvasVisible);
        Assert.True(NodeById(drill, 2).IsCanvasVisible);
        Assert.False(NodeById(drill, 3).IsCanvasVisible);
        Assert.Empty(drill.VisibleSubGraphs);
        Assert.False(EdgeBetween(drill, 1, 2).IsRenderHidden);
        Assert.True(EdgeBetween(drill, 2, 3).IsRenderHidden);

        // The root canvas is unchanged: G's proxy plus the ungrouped node 3.
        Assert.Same(g, Assert.Single(root.VisibleSubGraphs));
        Assert.False(NodeById(root, 1).IsCanvasVisible);
        Assert.True(NodeById(root, 3).IsCanvasVisible);
    }

    [Fact]
    public void GroupingInsideADrillInNestsUnderThatContext()
    {
        var grouping = new AssetGraphGroupingModel();
        var root = new AssetGraphEditorPaneViewModel(grouping: grouping);
        root.LoadSnapshot(SnapshotWithEdges([1, 2, 3]));
        root.SelectNode(NodeById(root, 1));
        root.ToggleNodeSelection(NodeById(root, 2));
        root.ToggleNodeSelection(NodeById(root, 3));
        var g = root.CreateSubGraphFromSelection("G");

        var drill = new AssetGraphEditorPaneViewModel(grouping: grouping, context: g);
        drill.LoadSnapshot(SnapshotWithEdges([1, 2, 3]));
        drill.SelectNode(NodeById(drill, 1));
        drill.ToggleNodeSelection(NodeById(drill, 2));
        var child = drill.CreateSubGraphFromSelection("Child");

        Assert.NotNull(child);
        Assert.Equal(g!.Id, child!.ParentId);

        // In G's drill-in, nodes 1 & 2 collapse behind the nested child proxy; 3 stays.
        Assert.Same(child, Assert.Single(drill.VisibleSubGraphs));
        Assert.False(NodeById(drill, 1).IsCanvasVisible);
        Assert.True(NodeById(drill, 3).IsCanvasVisible);

        // The root canvas still shows only G's top-level proxy.
        Assert.Same(g, Assert.Single(root.VisibleSubGraphs));
    }

    [Fact]
    public void GraphHeightGrowsForNodesWithManyPorts()
    {
        var pane = new AssetGraphEditorPaneViewModel();
        pane.LoadSnapshot(new EngineAssetGraphSnapshotResponse
        {
            Ok = true,
            Snapshot = new EngineAssetGraphSnapshot
            {
                Nodes =
                [
                    new EngineAssetGraphNode
                    {
                        Id = 1,
                        TypeName = "Fat",
                        DisplayName = "fat",
                        InputPorts = Enumerable
                            .Range(0, 8)
                            .Select(i => new EngineAssetGraphPort { Index = (uint)i })
                            .ToList(),
                    },
                ],
            },
        });

        // An 8-port card is far taller than the 116px minimum; the scroll bounds must
        // include it so its lower ports aren't clipped out of the scroll region.
        Assert.True(pane.GraphHeight > 116 + 28);
    }

    [Fact]
    public void InspectorRenamesSelectedSubGraphLive()
    {
        var subGraph = new AssetGraphSubGraph("id", "Old");
        var inspector = new InspectorPaneViewModel();

        inspector.Inspect(subGraph);
        Assert.True(inspector.HasSubGraphSelection);
        Assert.Equal("Old", inspector.SubGraphName);
        Assert.Equal("0", inspector.SubGraphMemberCount);

        inspector.SubGraphName = "Sky";
        Assert.Equal("Sky", subGraph.Name); // renamed live (the proxy card binds Name)
        Assert.Equal("Sky", inspector.Header);

        inspector.SubGraphName = "   "; // blank ignored — the name stays
        Assert.Equal("Sky", subGraph.Name);

        // Switching selection away clears the sub-graph inspection.
        inspector.Inspect((AssetGraphSubGraph?)null);
        Assert.False(inspector.HasSubGraphSelection);
    }

    // --- Sidecar persistence -----------------------------------------------------------

    [Fact]
    public void SidecarRoundTripsSubGraphsAndReroutes()
    {
        var model = new AssetGraphGroupingModel();
        var group = model.CreateSubGraph("Terrain", [1ul, 2ul], id: "terrain");
        group.ProxyX = 120;
        group.ProxyY = 240;

        var loaded = AssetGraphSubGraphSidecar.Deserialize(
            AssetGraphSubGraphSidecar.Serialize(
                model.SubGraphs,
                new Dictionary<ulong, string> { [7] = "SharedTex" }));

        var one = Assert.Single(loaded.SubGraphs);
        Assert.Equal("terrain", one.Id);
        Assert.Equal("Terrain", one.Name);
        Assert.Null(one.ParentId);
        Assert.Equal(120, one.ProxyX);
        Assert.Equal(240, one.ProxyY);
        Assert.Equal<ulong>([1, 2], one.MemberNodeIds.OrderBy(id => id).ToArray());

        var reroute = Assert.Single(loaded.Reroutes);
        Assert.Equal(7ul, reroute.SourceNodeId);
        Assert.Equal("SharedTex", reroute.Name);
    }

    [Fact]
    public void DeserializeReturnsEmptyForMissingOrMalformedJson()
    {
        Assert.Empty(AssetGraphSubGraphSidecar.Deserialize("").SubGraphs);
        Assert.Empty(AssetGraphSubGraphSidecar.Deserialize("}{ not json").Reroutes);
    }

    [Fact]
    public void LoadSubGraphsAppliesPersistedGroupsAndDropsDeadOnes()
    {
        var pane = new AssetGraphEditorPaneViewModel();
        pane.LoadSnapshot(SnapshotWithEdges([1, 2, 3], (1, 2)));

        pane.LoadSubGraphs(new[]
        {
            new PersistedSubGraph
            {
                Id = "g",
                Name = "G",
                ProxyX = 50,
                ProxyY = 60,
                MemberNodeIds = [1ul, 2ul],
            },
            new PersistedSubGraph
            {
                Id = "dead",
                Name = "Dead",
                MemberNodeIds = [4ul],
            },
        });

        // The 'dead' group referenced a node that no longer exists → reconciled away.
        var group = Assert.Single(pane.SubGraphs);
        Assert.Equal("G", group.Name);
        Assert.Equal(50, group.ProxyX);
        Assert.Equal(60, group.ProxyY);
        // Projection applied on load: members hidden, the rest visible.
        Assert.False(NodeById(pane, 1).IsCanvasVisible);
        Assert.False(NodeById(pane, 2).IsCanvasVisible);
        Assert.True(NodeById(pane, 3).IsCanvasVisible);
    }

    private static AssetGraphNodeCardViewModel NodeById(
        AssetGraphEditorPaneViewModel pane,
        ulong id) =>
        pane.Nodes.First(node => node.Id == id);

    private static AssetGraphEdgeViewModel EdgeBetween(
        AssetGraphEditorPaneViewModel pane,
        ulong from,
        ulong to) =>
        pane.Edges.First(edge => edge.FromNodeId == from && edge.ToNodeId == to);

    private static EngineAssetGraphSnapshotResponse SnapshotWithEdges(
        ulong[] nodeIds,
        params (ulong From, ulong To)[] edges) =>
        new()
        {
            Ok = true,
            Snapshot = new EngineAssetGraphSnapshot
            {
                Nodes = nodeIds
                    .Select((id, index) => new EngineAssetGraphNode
                    {
                        Id = id,
                        TypeName = "Node",
                        DisplayName = $"node {id}",
                        X = index * 300,
                        InputPorts = [new EngineAssetGraphPort { Index = 0, Label = "in" }],
                        OutputPorts = [new EngineAssetGraphPort { Index = 0, Label = "out" }],
                    })
                    .ToList(),
                Edges = edges
                    .Select((edge, index) => new EngineAssetGraphEdge
                    {
                        Id = (ulong)(index + 1),
                        From = edge.From,
                        To = edge.To,
                        ToInputPort = 0,
                    })
                    .ToList(),
            },
        };

    // --- Named reroutes ----------------------------------------------------------------

    [Fact]
    public void CreatingRerouteHidesFanOutAndBadgesPorts()
    {
        var pane = new AssetGraphEditorPaneViewModel();
        pane.LoadSnapshot(SnapshotWithEdges([1, 2, 3], (1, 2), (1, 3)));

        var source = NodeById(pane, 1);
        pane.CreateReroute(source);

        Assert.True(pane.IsReroute(1));
        // The source's fan-out wires are hidden.
        Assert.True(EdgeBetween(pane, 1, 2).IsRenderHidden);
        Assert.True(EdgeBetween(pane, 1, 3).IsRenderHidden);
        // The source output and each fed input carry the reroute badge (default = name).
        Assert.Equal("node 1", source.OutputPorts[0].RerouteName);
        Assert.Equal("node 1", NodeById(pane, 2).InputPorts[0].RerouteName);
        Assert.Equal("node 1", NodeById(pane, 3).InputPorts[0].RerouteName);
        // The name shows as a full-width badge line: a declaration on the source, and a
        // usage on each fed node that names the input port it feeds.
        Assert.Contains("→ node 1", source.RerouteBadges);
        Assert.Contains(NodeById(pane, 2).RerouteBadges, badge => badge.Contains("node 1"));
        Assert.Contains(NodeById(pane, 2).RerouteBadges, badge => badge.Contains("in"));

        pane.RemoveReroute(source);
        Assert.False(pane.IsReroute(1));
        Assert.False(EdgeBetween(pane, 1, 2).IsRenderHidden);
        Assert.Null(source.OutputPorts[0].RerouteName);
        Assert.Empty(source.RerouteBadges);
    }

    [Fact]
    public void LoadReroutesAppliesAndDropsDeadOnes()
    {
        var pane = new AssetGraphEditorPaneViewModel();
        pane.LoadSnapshot(SnapshotWithEdges([1, 2], (1, 2)));

        pane.LoadReroutes(new[]
        {
            new PersistedReroute { SourceNodeId = 1, Name = "Shared" },
            new PersistedReroute { SourceNodeId = 9, Name = "Dead" },
        });

        Assert.True(pane.IsReroute(1));
        Assert.False(pane.IsReroute(9)); // node 9 doesn't exist → reconciled away
        Assert.Equal("Shared", NodeById(pane, 1).OutputPorts[0].RerouteName);
        Assert.True(EdgeBetween(pane, 1, 2).IsRenderHidden);
    }

    [Fact]
    public void InspectorNamesAndClearsNodeReroute()
    {
        var reroutes = new AssetGraphRerouteModel();
        var inspector = new InspectorPaneViewModel();
        inspector.SetRerouteModel(reroutes);
        var changed = 0;
        inspector.RerouteChanged += () => changed++;

        var node = new AssetGraphNodeCardViewModel(
            new EngineAssetGraphNode { Id = 5, DisplayName = "src", TypeName = "N" },
            28.0);
        inspector.Inspect(node);

        Assert.True(inspector.HasAssetGraphNodeSelection);
        Assert.Equal(string.Empty, inspector.NodeRerouteName);

        inspector.NodeRerouteName = "SharedTex";
        Assert.Equal("SharedTex", reroutes.NameOf(5));
        Assert.True(changed > 0);

        inspector.NodeRerouteName = "   "; // clearing removes the reroute
        Assert.False(reroutes.IsReroute(5));
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
