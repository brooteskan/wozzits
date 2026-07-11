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

    public string Value
    {
        get => _get();
        set
        {
            if (_get() == value)
            {
                return;
            }

            _set(value);
            OnPropertyChanged();
            _edited?.Invoke();
        }
    }
}
