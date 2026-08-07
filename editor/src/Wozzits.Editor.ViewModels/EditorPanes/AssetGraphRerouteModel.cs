namespace Wozzits.Editor.ViewModels.EditorPanes;

// Editor-only "named reroute": a display transform that declutters a source node's
// fan-out. When a node's output is named, its outgoing edges render as name badges — a
// declaration at the source output, a usage chip at each destination input port — instead
// of drawn wires. The underlying edges are UNCHANGED, so this never touches asset
// compilation, keys, or the built graph (issue woguls/wozzits-editor#1).
//
// Keyed by source node id: the graph's edges identify their source by node only (there is
// no per-output-port index on an edge), so naming a node's output reroutes all of its
// outgoing wires — exactly the "single node feeds many input ports" case. Shared across
// panes; persisted in the sub-graph sidecar.
public sealed class AssetGraphRerouteModel
{
    private readonly Dictionary<ulong, string> _names = [];

    public IReadOnlyDictionary<ulong, string> Names => _names;

    public bool IsReroute(ulong sourceNodeId) => _names.ContainsKey(sourceNodeId);

    public string? NameOf(ulong sourceNodeId) => _names.GetValueOrDefault(sourceNodeId);

    public void Set(ulong sourceNodeId, string name) =>
        _names[sourceNodeId] = string.IsNullOrWhiteSpace(name) ? "reroute" : name.Trim();

    public void Remove(ulong sourceNodeId) => _names.Remove(sourceNodeId);

    // Drop reroutes whose source node no longer exists (called after the graph reloads).
    public void ReconcileWithLiveNodes(IReadOnlySet<ulong> liveNodeIds)
    {
        foreach (var id in _names.Keys.Where(id => !liveNodeIds.Contains(id)).ToList())
        {
            _names.Remove(id);
        }
    }

    public void Clear() => _names.Clear();

    public void Load(IEnumerable<KeyValuePair<ulong, string>> reroutes)
    {
        _names.Clear();
        foreach (var (id, name) in reroutes)
        {
            if (!string.IsNullOrWhiteSpace(name))
            {
                _names[id] = name;
            }
        }
    }
}
