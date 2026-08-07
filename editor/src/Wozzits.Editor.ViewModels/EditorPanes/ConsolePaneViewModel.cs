namespace Wozzits.Editor.ViewModels.EditorPanes;

public sealed class ConsolePaneViewModel : ViewModelBase
{
    private const int MaxLogLines = 500;

    private readonly List<string> _logLines = [];
    private string _cachedText = string.Empty;

    // Cached so the bound TextBlock re-reads an O(1) property instead of
    // re-joining every line on each notification.
    public string LogText => _cachedText;

    public void AppendLogLine(string line) => AppendLogLines(new[] { line });

    // Batch append: add every line, trim ONCE, rebuild the joined text ONCE, and
    // raise a SINGLE change notification. A coalesced UI-thread flush calls this
    // with a whole burst, so N engine log lines cost one re-render, not N (the old
    // per-line path did an O(n) RemoveAt(0) + full string.Join + relayout on every
    // single line, which froze the editor during a resolve).
    public void AppendLogLines(IReadOnlyList<string> lines)
    {
        var added = false;
        foreach (var line in lines)
        {
            if (string.IsNullOrWhiteSpace(line))
            {
                continue;
            }

            _logLines.Add(line);
            added = true;
        }

        if (!added)
        {
            return;
        }

        if (_logLines.Count > MaxLogLines)
        {
            _logLines.RemoveRange(0, _logLines.Count - MaxLogLines);
        }

        _cachedText = string.Join(Environment.NewLine, _logLines);
        OnPropertyChanged(nameof(LogText));
    }
}
