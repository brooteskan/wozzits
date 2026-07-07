namespace Wozzits.Editor.Core.Logging;

// A per-run file mirror of the editor console. Every line the console shows is
// ALSO written here -- uncapped (the UI keeps only the last N lines) and stamped
// with a wall-clock time, so the file is a complete, ordered timeline of a run
// (engine + editor + the separate play process, all of which converge on the
// console's AppendEditorLog). AutoFlush keeps the file complete even if a play
// session takes the editor down mid-run -- the exact case you want a log for.
//
// Construction never throws: if the log file cannot be opened (permissions, a
// locked path) the sink silently no-ops, so mirroring to a file can never be the
// thing that breaks the editor.
public sealed class FileLogSink : IDisposable
{
    private readonly object _gate = new();
    private StreamWriter? _writer;

    // The file actually opened, or null if the sink failed to open one (in which
    // case Write no-ops). Lets the caller surface "logging to <path>" in the UI.
    public string? FilePath { get; }

    public FileLogSink(string directory, string fileNameStem)
    {
        try
        {
            Directory.CreateDirectory(directory);
            var path = ResolveUniquePath(directory, fileNameStem);
            _writer = new StreamWriter(
                new FileStream(
                    path, FileMode.CreateNew, FileAccess.Write, FileShare.Read))
            {
                AutoFlush = true,
            };
            FilePath = path;
        }
        catch
        {
            _writer = null;
            FilePath = null;
        }
    }

    // %LOCALAPPDATA%\Wozzits\logs\wozzits-editor-<yyyyMMdd-HHmmss>.log -- one file
    // per run, out of the repo and the project (no scratch in the tree).
    public static FileLogSink CreateDefault()
    {
        var directory = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "Wozzits",
            "logs");
        var stem = "wozzits-editor-" + DateTime.Now.ToString("yyyyMMdd-HHmmss");
        return new FileLogSink(directory, stem);
    }

    public void Write(string line)
    {
        if (string.IsNullOrEmpty(line))
        {
            return;
        }

        lock (_gate)
        {
            if (_writer is null)
            {
                return;
            }

            _writer.Write(DateTime.Now.ToString("HH:mm:ss.fff"));
            _writer.Write(' ');
            _writer.WriteLine(line);
        }
    }

    // Two runs in the same second (a second editor window) would collide on the
    // second-resolution stem; disambiguate with a counter rather than clobber.
    private static string ResolveUniquePath(string directory, string stem)
    {
        var path = Path.Combine(directory, stem + ".log");
        var suffix = 1;
        while (File.Exists(path))
        {
            path = Path.Combine(directory, $"{stem}-{suffix}.log");
            suffix++;
        }

        return path;
    }

    public void Dispose()
    {
        lock (_gate)
        {
            _writer?.Dispose();
            _writer = null;
        }
    }
}
