namespace Wozzits.Editor.ViewModels.EditorPanes;

public sealed class ConsolePaneViewModel : ViewModelBase
{
    private const int MaxLogLines = 500;

    private readonly List<string> _logLines = [];

    public string LogText =>
        _logLines.Count == 0
            ? string.Empty
            : string.Join(Environment.NewLine, _logLines);

    public void AppendLogLine(string line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return;
        }

        _logLines.Add(line);
        while (_logLines.Count > MaxLogLines)
        {
            _logLines.RemoveAt(0);
        }

        OnPropertyChanged(nameof(LogText));
    }
}
