namespace Wozzits.Editor.ViewModels.EditorPanes;

using System.Collections.ObjectModel;

// Pure, engine-independent model of editor-only node grouping for the asset graph.
// Maintains the "a node belongs to at most one sub-graph" invariant plus a reverse index
// for O(1) membership lookup. Knows nothing about node positions, the engine session, or
// the asset compiler — grouping is editor-only view-state (issue woguls/wozzits-editor#1).
public sealed class AssetGraphGroupingModel
{
    private readonly ObservableCollection<AssetGraphSubGraph> _subGraphs = [];
    private readonly Dictionary<ulong, AssetGraphSubGraph> _nodeToSubGraph = [];

    public AssetGraphGroupingModel()
    {
        SubGraphs = new ReadOnlyObservableCollection<AssetGraphSubGraph>(_subGraphs);
    }

    public ReadOnlyObservableCollection<AssetGraphSubGraph> SubGraphs { get; }

    // The sub-graph that directly owns nodeId, or null if it is ungrouped.
    public AssetGraphSubGraph? SubGraphOfNode(ulong nodeId) =>
        _nodeToSubGraph.GetValueOrDefault(nodeId);

    public AssetGraphSubGraph? FindById(string subGraphId) =>
        _subGraphs.FirstOrDefault(subGraph => subGraph.Id == subGraphId);

    // Create a new sub-graph owning the given nodes. Any node already in another sub-graph
    // is moved into this one (exactly-one membership); a prior sub-graph left with no
    // members and no children is removed. Throws if no members are supplied.
    public AssetGraphSubGraph CreateSubGraph(
        string name,
        IEnumerable<ulong> memberNodeIds,
        string? parentId = null,
        string? id = null)
    {
        var members = memberNodeIds.Distinct().ToList();
        if (members.Count == 0)
        {
            throw new ArgumentException(
                "A sub-graph must contain at least one node.",
                nameof(memberNodeIds));
        }

        var subGraph = new AssetGraphSubGraph(
            id ?? Guid.NewGuid().ToString("n"),
            name,
            parentId);

        foreach (var nodeId in members)
        {
            Reassign(nodeId, subGraph);
        }

        _subGraphs.Add(subGraph);
        return subGraph;
    }

    // Add nodes to an existing sub-graph, moving each out of any prior sub-graph. No-op if
    // the id is unknown.
    public void AddNodes(string subGraphId, IEnumerable<ulong> nodeIds)
    {
        var subGraph = FindById(subGraphId);
        if (subGraph is null)
        {
            return;
        }

        foreach (var nodeId in nodeIds.Distinct())
        {
            Reassign(nodeId, subGraph);
        }
    }

    // Remove a single node from whatever sub-graph owns it. A sub-graph left with no
    // members and no children is removed.
    public void RemoveNode(ulong nodeId)
    {
        if (!_nodeToSubGraph.TryGetValue(nodeId, out var subGraph))
        {
            return;
        }

        _nodeToSubGraph.Remove(nodeId);
        subGraph.MemberNodeIds.Remove(nodeId);
        RemoveIfEmpty(subGraph);
    }

    // Dissolve a sub-graph: its member nodes become ungrouped and any child sub-graphs
    // reparent to this sub-graph's parent. Returns false if the id is unknown.
    public bool Ungroup(string subGraphId)
    {
        var subGraph = FindById(subGraphId);
        if (subGraph is null)
        {
            return false;
        }

        foreach (var nodeId in subGraph.MemberNodeIds)
        {
            _nodeToSubGraph.Remove(nodeId);
        }

        subGraph.MemberNodeIds.Clear();

        foreach (var child in _subGraphs)
        {
            if (child.ParentId == subGraph.Id)
            {
                child.ParentId = subGraph.ParentId;
            }
        }

        _subGraphs.Remove(subGraph);
        return true;
    }

    // Drop membership for node ids that no longer exist in the live graph, removing any
    // sub-graph left empty. Call after the graph reloads from the engine so deleted nodes
    // (and stale groups from a previously-open project) do not linger.
    public void ReconcileWithLiveNodes(IReadOnlySet<ulong> liveNodeIds)
    {
        var dead = _nodeToSubGraph.Keys
            .Where(nodeId => !liveNodeIds.Contains(nodeId))
            .ToList();

        foreach (var nodeId in dead)
        {
            var subGraph = _nodeToSubGraph[nodeId];
            _nodeToSubGraph.Remove(nodeId);
            subGraph.MemberNodeIds.Remove(nodeId);
        }

        foreach (var subGraph in _subGraphs.ToList())
        {
            RemoveIfEmpty(subGraph);
        }
    }

    // Drop all sub-graphs (e.g. when switching projects).
    public void Clear()
    {
        _nodeToSubGraph.Clear();
        _subGraphs.Clear();
    }

    private void Reassign(ulong nodeId, AssetGraphSubGraph target)
    {
        if (_nodeToSubGraph.TryGetValue(nodeId, out var current))
        {
            if (ReferenceEquals(current, target))
            {
                return;
            }

            current.MemberNodeIds.Remove(nodeId);
            RemoveIfEmpty(current);
        }

        target.MemberNodeIds.Add(nodeId);
        _nodeToSubGraph[nodeId] = target;
    }

    private void RemoveIfEmpty(AssetGraphSubGraph subGraph)
    {
        if (subGraph.MemberNodeIds.Count > 0)
        {
            return;
        }

        // A sub-graph that still parents nested sub-graphs is not empty.
        if (_subGraphs.Any(candidate => candidate.ParentId == subGraph.Id))
        {
            return;
        }

        _subGraphs.Remove(subGraph);
    }
}
