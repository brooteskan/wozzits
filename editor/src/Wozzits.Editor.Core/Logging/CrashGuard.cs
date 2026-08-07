using System.Text;

namespace Wozzits.Editor.Core.Logging;

// The editor's last line of defence: an exception that reaches the top gets
// WRITTEN DOWN and, where the runtime allows it, does not take the window with
// it (D3-Q1 / D3-C7, ruled 2026-08-02 -- log and continue, the editor should
// never die).
//
// Before this there was no handler of any kind: zero hits for
// AppDomain.CurrentDomain.UnhandledException, Dispatcher.UIThread.UnhandledException
// and TaskScheduler.UnobservedTaskException across the whole editor, and
// Program.cs was the unmodified Avalonia template. So the editor died silently,
// and it died without writing anything into FileLogSink -- whose own comment says
// it exists so a run survives being taken down mid-flight. The one artefact built
// to diagnose a crash never received one.
//
// WHAT "NEVER DIE" CAN AND CANNOT MEAN, because the difference is not a detail:
//
//   * UI-thread exceptions -- Dispatcher.UIThread.UnhandledException, marked
//     Handled -- ARE survivable, and that is the case that matters. All six
//     `async void`-shaped handlers in the editor post their exceptions to the UI
//     dispatcher, including the four in Views/EditorPanes/ that have no route to
//     the editor log at all. Those are the ones that were silent AND fatal.
//   * Unobserved Task exceptions are already non-fatal on modern .NET; observing
//     them here is belt-and-braces, and the value is the log line.
//   * AppDomain.CurrentDomain.UnhandledException CANNOT be prevented. By the time
//     it fires the runtime is already tearing the process down and there is no
//     supported way to cancel that. StackOverflowException and a few others
//     cannot even be observed. So that hook is a RECORDER, not a rescue -- but
//     recording is exactly what was missing, since this is the death that
//     currently leaves nothing behind.
//
// Surviving an exception means the operation that threw was abandoned partway,
// so the editor may be left in an inconsistent state. That is the accepted trade:
// a live window the user can save from beats a dead one, and the log line says
// what happened so the state is not a mystery.
public static class CrashGuard
{
    private static readonly object Gate = new();
    private static Action<string>? _sink;
    private static bool _installed;

    // How many times Install() actually subscribed. Exists only so the idempotence
    // test can assert something REAL: a test that calls Report() directly never
    // touches the event subscriptions and would pass with the guard removed.
    internal static int InstallCount { get; private set; }

    // Reporting must not re-enter: the sink runs arbitrary code (it ends up in
    // the editor console and the file mirror), and if THAT throws we would be
    // handling an exception raised while handling an exception, forever. Thread
    // static because reports can arrive on several threads at once and one
    // thread's recursion must not mute another's report.
    [ThreadStatic]
    private static bool _reporting;

    // Hook the process-wide events that do not need a UI toolkit. Call as early
    // as possible -- before Avalonia is built, so a failure during startup is
    // covered too. Idempotent: calling twice does not double-report.
    //
    // The Avalonia dispatcher hook is NOT here, because Core deliberately has no
    // dependencies. The App layer installs it and calls Report; see App.axaml.cs.
    public static void Install()
    {
        lock (Gate)
        {
            if (_installed)
            {
                return;
            }
            _installed = true;
            InstallCount++;
        }

        AppDomain.CurrentDomain.UnhandledException += (_, args) =>
        {
            // args.IsTerminating is informational -- nothing here can stop it.
            Report(
                "AppDomain.UnhandledException",
                args.ExceptionObject as Exception,
                survived: !args.IsTerminating);
        };

        TaskScheduler.UnobservedTaskException += (_, args) =>
        {
            Report("TaskScheduler.UnobservedTaskException", args.Exception, survived: true);
            args.SetObserved();
        };
    }

    // Route reports into the editor console once one exists. The console is
    // mirrored to FileLogSink, so this single call gets both the visible line and
    // the persistent one, in order with everything around it -- which is worth
    // more for diagnosis than a separate crash file would be.
    //
    // Until a project window is up (the folder picker at App.axaml.cs:50 runs
    // before any of this) reports fall back to the log directory directly.
    public static void SetSink(Action<string> sink)
    {
        ArgumentNullException.ThrowIfNull(sink);
        lock (Gate)
        {
            _sink = sink;
        }
    }

    // Public so the App layer's Avalonia dispatcher hook shares this exact path
    // rather than growing a second, differently-formatted one.
    public static void Report(string origin, Exception? error, bool survived)
    {
        if (_reporting)
        {
            return;
        }

        _reporting = true;
        try
        {
            var text = Describe(origin, error, survived);

            Action<string>? sink;
            lock (Gate)
            {
                sink = _sink;
            }

            if (sink is not null)
            {
                try
                {
                    sink(text);
                    return;
                }
                catch
                {
                    // The console itself is broken; fall through to the file so
                    // the report is not lost with it.
                }
            }

            WriteFallback(text);
        }
        catch
        {
            // A crash reporter that throws is worse than one that says nothing.
        }
        finally
        {
            _reporting = false;
        }
    }

    // Separated so the wording is testable without installing anything.
    internal static string Describe(string origin, Exception? error, bool survived)
    {
        var text = new StringBuilder();
        text.Append("[editor] UNHANDLED EXCEPTION (")
            .Append(origin)
            .Append("): ")
            .Append(survived
                ? "the editor is still running, but the operation that threw was "
                  + "abandoned partway -- save your work and restart when you can."
                : "the process is terminating and this could not be prevented.");

        if (error is null)
        {
            text.Append(Environment.NewLine)
                .Append("  (no exception object was supplied)");
            return text.ToString();
        }

        // Every level, because the one that matters is usually innermost and an
        // AggregateException from a Task hides it behind a useless wrapper.
        var depth = 0;
        for (var current = error; current is not null; current = current.InnerException)
        {
            text.Append(Environment.NewLine)
                .Append("  ")
                .Append(depth == 0 ? string.Empty : "-> ")
                .Append(current.GetType().FullName)
                .Append(": ")
                .Append(current.Message);

            if (!string.IsNullOrWhiteSpace(current.StackTrace))
            {
                text.Append(Environment.NewLine).Append(current.StackTrace);
            }

            if (++depth >= 8)
            {
                text.Append(Environment.NewLine).Append("  (inner chain truncated)");
                break;
            }
        }

        return text.ToString();
    }

    private static void WriteFallback(string text)
    {
        try
        {
            var directory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "Wozzits",
                "logs");
            Directory.CreateDirectory(directory);
            File.AppendAllText(
                Path.Combine(directory, "wozzits-editor-crash.log"),
                $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} {text}{Environment.NewLine}");
        }
        catch
        {
            // Nowhere left to write. Do not make it worse.
        }

        try
        {
            Console.Error.WriteLine(text);
        }
        catch
        {
        }
    }
}
