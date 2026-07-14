namespace Wozzits.Editor.ViewModels.EditorPanes.Minds;

using System;
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
    }

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
