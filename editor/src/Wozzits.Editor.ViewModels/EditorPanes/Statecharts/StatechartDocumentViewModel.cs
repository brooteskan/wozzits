namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

using System.IO;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.Statecharts;

// One chart, both layers, one document. Owns the Chart and its .sc.json path, so it can save
// edits back to the chart and hand-placed layout to a <chart>.sc.editor.json sidecar. The
// control and dataflow canvases share the one Chart instance (edits land in one chart).
public sealed class StatechartDocumentViewModel : ViewModelBase
{
    private readonly Chart _chart;
    private string _path;
    private string _name;
    private bool _isReadOnly;
    private bool _metaDirty;

    public StatechartDocumentViewModel(
        string name,
        string path,
        Chart chart,
        IReadOnlyList<EngineActuator>? actuatorCatalog = null)
    {
        _name = name;
        _path = path;
        _chart = chart;
        NameEditor = new EditableFieldViewModel("name", () => Name, TryRename);
        Control = new ControlPaneViewModel();
        Dataflow = new DataflowPaneViewModel();
        // Set before Project so `call` effects get their actuator picker + arg pickers.
        if (actuatorCatalog is not null)
        {
            Control.ActuatorCatalog = actuatorCatalog;
        }
        Control.Project(chart);
        Dataflow.Project(chart);

        // A dataflow-pane structural edit (op/agent/binding add, delete, or rename) changes
        // the id vocabulary the control pane's effect pickers offer, and those lists are
        // derived at projection time — reproject the control pane so e.g. a renamed op
        // shows under its new name in a Call arg's source dropdown immediately.
        Dataflow.VocabularyChanged += Control.ReprojectPreservingSelection;

        // Cross-layer focus: selecting a state on the control canvas dims the dataflow to
        // just the ops feeding that state (David's "simplify the dataflow by selection").
        Control.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(ControlPaneViewModel.SelectedState))
            {
                Dataflow.FocusOnState(Control.SelectedState?.Model);
            }
        };

        ApplySavedLayout();
    }

    public string Name
    {
        get => _name;
        private set => SetProperty(ref _name, value);
    }

    // The chart compiled to the minified chart_ir a statechart_runner embeds. Used to refresh
    // attached runners on save so an edited chart takes effect (a runner runs a compiled snapshot,
    // not the .sc.json file -- without this you'd have to re-attach after every edit).
    //
    // Unvalidated on purpose: a property that throws is a trap for the UI, and the one caller that
    // PERSISTS this reads it only after Save() returned, which validates first. Anything new that
    // embeds this must go through Save() (or EmitValidated) rather than reading it cold.
    public string CompiledIr => StatechartJson.Emit(_chart, indented: false);

    // The same IR, but refusing what parse_chart would refuse. Save() validates
    // ONLY when the chart itself changed -- a layout-only save (pan / zoom / drag,
    // which set IsLayoutDirty but not IsDirty) or a read-only chart skips
    // EmitValidated entirely, while IsDirty is still true, so the persisting
    // reader still runs (D3-C29). That made the contract in CompiledIr's comment
    // above false in exactly the case nobody would think to check. Callers that
    // EMBED the IR -- into runners, into scenelet files -- must use this.
    public string ValidatedIr() => StatechartJson.EmitValidated(_chart, indented: false);

    // Editable name shown in the document header; committing it renames the chart's files.
    public EditableFieldViewModel NameEditor { get; }

    // Invoked after a successful rename so the host can retitle the tab + refresh the menu.
    public Action? Renamed { get; set; }

    // Invoked after the chart's files are deleted so the host can close the tab + refresh the menu.
    public Action? Deleted { get; set; }

    private IRelayCommand? _deleteChartCommand;

    public IRelayCommand DeleteChartCommand => _deleteChartCommand ??= new RelayCommand(DeleteChart);

    // Delete the chart's files (.sc.json + layout sidecar) and signal the host. A no-op when
    // read-only, so a protected chart can't be deleted.
    public void DeleteChart()
    {
        if (IsReadOnly)
        {
            return;
        }

        try
        {
            if (File.Exists(_path))
            {
                File.Delete(_path);
            }
            if (File.Exists(LayoutPath))
            {
                File.Delete(LayoutPath);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return;
        }

        Deleted?.Invoke();
    }

    public ControlPaneViewModel Control { get; }

    public DataflowPaneViewModel Dataflow { get; }

    // Protected chart: Save leaves the .sc.json untouched (only the sidecar persists the flag +
    // layout). Toggling it needs persisting, so it marks the document dirty.
    public bool IsReadOnly
    {
        get => _isReadOnly;
        set
        {
            if (SetProperty(ref _isReadOnly, value))
            {
                _metaDirty = true;
            }
        }
    }

    // The chart was edited (needs a .sc.json rewrite) or the layout / read-only flag changed.
    public bool IsDirty => Control.IsDirty || Control.IsLayoutDirty || Dataflow.IsDirty || Dataflow.IsLayoutDirty || _metaDirty;

    private string LayoutPath => Path.ChangeExtension(_path, ".editor.json");

    // Rename the chart: move its .sc.json (and layout sidecar) and update the name. A no-op when
    // read-only, or when the new name is empty / unchanged / invalid / already taken.
    public void TryRename(string newName)
    {
        if (IsReadOnly)
        {
            return;
        }

        newName = (newName ?? string.Empty).Trim();
        if (newName.Length == 0 || newName == Name || newName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            return;
        }

        var dir = Path.GetDirectoryName(_path);
        if (dir is null)
        {
            return;
        }

        var newPath = Path.Combine(dir, newName + ".sc.json");
        if (File.Exists(newPath))
        {
            return;   // a chart by that name already exists
        }

        try
        {
            var oldSidecar = LayoutPath;
            File.Move(_path, newPath);
            _path = newPath;
            if (File.Exists(oldSidecar))
            {
                File.Move(oldSidecar, LayoutPath);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return;
        }

        _chart.Name = newName;
        Name = newName;
        Renamed?.Invoke();
    }

    // Write the chart back to its .sc.json (only when actually edited, so an untouched chart is
    // never reformatted) and the hand-placed layout to the editor-owned sidecar.
    //
    // Throws StatechartFormatException when the chart is structurally invalid, BEFORE writing
    // anything -- so a chart the engine's parse_chart would refuse is never persisted, and the
    // document stays dirty for the user to fix rather than half-saving. The caller reports.
    public void Save()
    {
        if (!IsReadOnly && (Control.IsDirty || Dataflow.IsDirty))
        {
            File.WriteAllText(_path, StatechartJson.EmitValidated(_chart, indented: true));
        }

        File.WriteAllText(LayoutPath, CaptureLayout().ToJson());
        Control.ClearDirty();
        Dataflow.ClearDirty();
        _metaDirty = false;
    }

    private StatechartLayout CaptureLayout()
    {
        // Each pane snapshots its own half; merge into one sidecar layout.
        var layout = Control.CaptureLayout();
        var dataflow = Dataflow.CaptureLayout();
        layout.DataflowZoom = dataflow.DataflowZoom;
        layout.ReadOnly = IsReadOnly;
        foreach (var (id, point) in dataflow.NodePositions)
        {
            layout.NodePositions[id] = point;
        }
        return layout;
    }

    private void ApplySavedLayout()
    {
        if (!File.Exists(LayoutPath))
        {
            return;
        }

        string json;
        try
        {
            json = File.ReadAllText(LayoutPath);
        }
        catch (IOException)
        {
            return;
        }

        var layout = StatechartLayout.FromJson(json);
        _isReadOnly = layout.ReadOnly;
        Control.ApplyLayout(layout);
        Dataflow.ApplyLayout(layout);
    }
}
