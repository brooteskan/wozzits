namespace Wozzits.Editor.ViewModels.EditorPanes.Minds;

using System;
using System.Globalization;
using System.IO;
using CommunityToolkit.Mvvm.Input;
using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// One mind, one document. Owns the Mind + its .mind.json path, so it can save edits back to
// the .mind.json and the hand-placed layout to a <mind>.mind.editor.json sidecar. The canvas
// pane edits the one Mind instance. Parallel to StatechartDocumentViewModel (a mind has a
// single canvas, so this is simpler -- no control/dataflow split).
public sealed class MindDocumentViewModel : ViewModelBase
{
    private readonly Mind _mind;
    private string _path;
    private string _name;

    public MindDocumentViewModel(string name, string path, Mind mind)
    {
        _name = name;
        _path = path;
        _mind = mind;
        NameEditor = new EditableFieldViewModel("name", () => Name, TryRename);
        Pane = new MindPaneViewModel();
        Pane.Project(mind);
        ApplySavedLayout();

        // Global mind params -- edited in the document's properties panel (not per-qubit).
        // Each mutates the Mind + marks the pane dirty so Save rewrites the .mind.json.
        ChiEditor = IntField(() => _mind.Chi, c => { _mind.Chi = Math.Max(0, c); OnPropertyChanged(nameof(BackendLabel)); Pane.NotifyParamsChanged(); });
        MemoryEditor = IntField(() => _mind.Memory, m => _mind.Memory = Math.Max(0, m));
        GammaStartEditor = DoubleField(() => _mind.Clock.GammaStart, v => _mind.Clock.GammaStart = v);
        AnnealSecondsEditor = DoubleField(() => _mind.Clock.AnnealSeconds, v => _mind.Clock.AnnealSeconds = v);
        RelaxRateEditor = DoubleField(() => _mind.Clock.RelaxRate, v => _mind.Clock.RelaxRate = v);
        ConfidenceEditor = DoubleField(() => _mind.Commit.Confidence, v => _mind.Commit.Confidence = v);
        DecoherenceEditor = DoubleField(() => _mind.Commit.Decoherence, v => _mind.Commit.Decoherence = v);
    }

    public EditableFieldViewModel ChiEditor { get; }

    public EditableFieldViewModel MemoryEditor { get; }

    public EditableFieldViewModel GammaStartEditor { get; }

    public EditableFieldViewModel AnnealSecondsEditor { get; }

    public EditableFieldViewModel RelaxRateEditor { get; }

    public EditableFieldViewModel ConfidenceEditor { get; }

    public EditableFieldViewModel DecoherenceEditor { get; }

    // What backend the chi classifies into, with its constraint -- shown next to chi.
    public string BackendLabel => _mind.Backend switch
    {
        MindBackend.Exact => "exact — entangled, small groups, any graph",
        MindBackend.LoopyBp => "loopy BP — any graph, no entanglement",
        _ => "TTN — entangled, any graph; a chain is cheapest",
    };

    private EditableFieldViewModel IntField(Func<int> get, Action<int> set) =>
        new(string.Empty,
            () => get().ToString(CultureInfo.InvariantCulture),
            v =>
            {
                if (int.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out var x))
                {
                    set(x);
                    Pane.MarkDirty();
                }
            });

    private EditableFieldViewModel DoubleField(Func<double> get, Action<double> set) =>
        new(string.Empty,
            () => get().ToString("0.###", CultureInfo.InvariantCulture),
            v =>
            {
                if (double.TryParse(v, NumberStyles.Float, CultureInfo.InvariantCulture, out var x))
                {
                    set(x);
                    Pane.MarkDirty();
                }
            });

    public string Name
    {
        get => _name;
        private set => SetProperty(ref _name, value);
    }

    public MindPaneViewModel Pane { get; }

    // The mind compiled to the minified mind_ir a quantum_agent embeds (seam 3 wires it).
    public string CompiledIr => MindJson.Emit(_mind, indented: false);

    public EditableFieldViewModel NameEditor { get; }

    public Action? Renamed { get; set; }

    public Action? Deleted { get; set; }

    private IRelayCommand? _deleteMindCommand;

    public IRelayCommand DeleteMindCommand => _deleteMindCommand ??= new RelayCommand(DeleteMind);

    public bool IsDirty => Pane.IsDirty || Pane.IsLayoutDirty;

    private string LayoutPath => Path.ChangeExtension(_path, ".editor.json");

    // Rename the mind: move its .mind.json (+ layout sidecar) and update the name. A no-op
    // when the new name is empty / unchanged / invalid / already taken.
    public void TryRename(string newName)
    {
        newName = (newName ?? string.Empty).Trim();
        if (newName.Length == 0 || newName == Name
            || newName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            return;
        }

        var dir = Path.GetDirectoryName(_path);
        if (dir is null)
        {
            return;
        }

        var newPath = Path.Combine(dir, newName + ".mind.json");
        if (File.Exists(newPath))
        {
            return;   // a mind by that name already exists
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

        _mind.Name = newName;
        Name = newName;
        Renamed?.Invoke();
    }

    // Write the mind back to its .mind.json (only when actually edited) and the hand-placed
    // layout to the editor-owned sidecar.
    public void Save()
    {
        if (Pane.IsDirty)
        {
            File.WriteAllText(_path, MindJson.Emit(_mind, indented: true));
        }

        File.WriteAllText(LayoutPath, Pane.CaptureLayout().ToJson());
        Pane.ClearDirty();
    }

    public void DeleteMind()
    {
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

        Pane.ApplyLayout(MindLayout.FromJson(json));
    }
}
