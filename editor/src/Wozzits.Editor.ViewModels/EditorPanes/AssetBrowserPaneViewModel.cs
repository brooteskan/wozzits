using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.ViewModels.EditorPanes;

// Authoring palette: each asset type is a collapsible group whose schemas are
// individually draggable onto the asset graph, where they become node cards.
// The catalog already excludes asset types whose GPU residency has not migrated
// to wozzits-rhi (#186).
public sealed class AssetBrowserPaneViewModel : ViewModelBase
{
    private readonly List<AssetBrowserTypeViewModel> _allTypes = [];
    private string _searchText = string.Empty;
    private string _catalogEmptyState = "Asset catalog unavailable.";
    private string _emptyState = "Asset catalog unavailable.";

    public AssetBrowserPaneViewModel(
        IWozzitsEngineEditorSession? editorSession = null)
    {
        if (editorSession is not null)
        {
            Load(editorSession.LoadAssetCatalog());
        }
    }

    public ObservableCollection<AssetBrowserTypeViewModel> Types { get; } = [];

    public bool HasTypes => Types.Count > 0;

    public bool HasNoTypes => !HasTypes;

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value))
            {
                ApplyFilter();
            }
        }
    }

    public string EmptyState
    {
        get => _emptyState;
        private set => SetProperty(ref _emptyState, value);
    }

    public void Load(EngineAssetCatalogResponse? catalog)
    {
        _allTypes.Clear();

        if (catalog is null || !catalog.Ok)
        {
            _catalogEmptyState = string.IsNullOrWhiteSpace(catalog?.Error)
                ? "Asset catalog unavailable."
                : catalog!.Error;
        }
        else
        {
            foreach (var entry in catalog.Entries
                .OrderBy(entry => entry.Category, System.StringComparer.Ordinal)
                .ThenBy(entry => entry.TypeName, System.StringComparer.Ordinal))
            {
                _allTypes.Add(new AssetBrowserTypeViewModel(entry));
            }

            _catalogEmptyState = _allTypes.Count > 0
                ? string.Empty
                : "No assets available to add.";
        }

        ApplyFilter();
    }

    // Rebuild the visible Types from the full catalog, keeping only the groups
    // that match the search text (by type name, category, or any schema label).
    private void ApplyFilter()
    {
        Types.Clear();

        var query = _searchText.Trim();
        foreach (var type in _allTypes)
        {
            if (query.Length == 0 || Matches(type, query))
            {
                Types.Add(type);
            }
        }

        EmptyState = Types.Count > 0
            ? string.Empty
            : query.Length > 0
                ? $"No assets match \"{query}\"."
                : _catalogEmptyState;

        OnPropertyChanged(nameof(HasTypes));
        OnPropertyChanged(nameof(HasNoTypes));
    }

    private static bool Matches(AssetBrowserTypeViewModel type, string query)
    {
        if (Contains(type.TypeName, query) || Contains(type.Category, query))
        {
            return true;
        }
        foreach (var schema in type.Schemas)
        {
            if (Contains(schema.Label, query) || Contains(schema.TypeName, query))
            {
                return true;
            }
        }
        return false;
    }

    private static bool Contains(string text, string query)
    {
        return text.Contains(query, System.StringComparison.OrdinalIgnoreCase);
    }
}

// A collapsible asset-type group (e.g. "Mesh") with its schemas underneath.
public sealed class AssetBrowserTypeViewModel
{
    public AssetBrowserTypeViewModel(EngineAssetCatalogEntry entry)
    {
        TypeName = entry.TypeName;
        Category = entry.Category;
        TypeId = entry.Type;
        Header = entry.Schemas.Count == 1
            ? TypeName
            : $"{TypeName}  ({entry.Schemas.Count})";

        foreach (var schema in entry.Schemas
            .OrderBy(schema => schema.Label, System.StringComparer.Ordinal))
        {
            Schemas.Add(new AssetBrowserSchemaViewModel(
                schema,
                entry.Type,
                entry.TypeName,
                entry.Category));
        }
    }

    public string TypeName { get; }

    public string Category { get; }

    public int TypeId { get; }

    public string Header { get; }

    public ObservableCollection<AssetBrowserSchemaViewModel> Schemas { get; } = [];
}

// One draggable schema. Dropped on the asset graph, it adds a node for
// (Schema, Type). Carries everything the drop target needs.
public sealed class AssetBrowserSchemaViewModel
{
    public AssetBrowserSchemaViewModel(
        EngineAssetCatalogSchema schema,
        int type,
        string typeName,
        string category)
    {
        Label = schema.Label;
        Schema = schema.Schema;
        Type = type;
        TypeName = typeName;
        Category = category;
        Detail = $"{category} | type {type.ToString(CultureInfo.InvariantCulture)}";
    }

    public string Label { get; }

    public ulong Schema { get; }

    public int Type { get; }

    public string TypeName { get; }

    public string Category { get; }

    public string Detail { get; }
}
