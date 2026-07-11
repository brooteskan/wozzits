namespace Wozzits.Editor.ViewModels.EditorPanes;

using System.Collections.ObjectModel;

// An editor-only, named grouping of asset-graph nodes (a "sub-graph"). Membership is
// keyed by the stable engine node id (ulong). Nesting is modelled from the start via
// ParentId (a sub-graph may live inside another); one level is exercised first.
//
// This is pure editor view-state: it never participates in asset compilation, key
// generation, or the built graph (issue woguls/wozzits-editor#1). It is persisted to an
// editor-owned sidecar, kept separate from the engine-authored asset graph JSON.
public sealed class AssetGraphSubGraph : ViewModelBase
{
    private string _name;
    private string? _parentId;
    private double _proxyX;
    private double _proxyY;

    public AssetGraphSubGraph(string id, string name, string? parentId = null)
    {
        Id = id;
        _name = name;
        _parentId = parentId;
    }

    // Stable editor-assigned identity, independent of node ids and engine asset keys.
    public string Id { get; }

    public string Name
    {
        get => _name;
        set => SetProperty(ref _name, value);
    }

    // The containing sub-graph's Id, or null when this sub-graph sits on the root canvas.
    public string? ParentId
    {
        get => _parentId;
        set => SetProperty(ref _parentId, value);
    }

    // Canvas position of the collapsed proxy card on its parent canvas.
    public double ProxyX
    {
        get => _proxyX;
        set => SetProperty(ref _proxyX, value);
    }

    public double ProxyY
    {
        get => _proxyY;
        set => SetProperty(ref _proxyY, value);
    }

    // Stable engine node ids that belong directly to this sub-graph.
    public ObservableCollection<ulong> MemberNodeIds { get; } = [];
}
