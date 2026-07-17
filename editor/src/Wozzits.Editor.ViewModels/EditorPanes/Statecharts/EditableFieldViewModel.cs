namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// A two-way inspector field: a display Name + an editable string Value that reads/writes a
// model value through the supplied accessors and fires an on-edited callback on commit (the
// view binds with UpdateSourceTrigger=LostFocus, so the callback runs once per committed edit).
public sealed class EditableFieldViewModel : ViewModelBase
{
    private readonly Func<string> _get;
    private readonly Action<string> _set;
    private readonly Action? _edited;

    public EditableFieldViewModel(string name, Func<string> get, Action<string> set, Action? edited = null)
    {
        Name = name;
        _get = get;
        _set = set;
        _edited = edited;
    }

    public string Name { get; }

    // The raw edited text, cached so a PropertyChanged-triggered binding echoes exactly
    // what the user typed rather than the model's reformat of it (e.g. typing "0." must
    // not snap back to "0" and eat the decimal). The field VM is rebuilt on reproject, so
    // a fresh instance reads the model via _get(); the cache only lives across keystrokes.
    private string? _raw;

    public string Value
    {
        get => _raw ??= _get();
        set
        {
            if ((_raw ?? _get()) == value)
            {
                return;
            }

            _raw = value;
            _set(value);
            OnPropertyChanged();
            _edited?.Invoke();
        }
    }
}
