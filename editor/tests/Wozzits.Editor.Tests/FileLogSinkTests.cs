using System.Reflection;
using Wozzits.Editor.Core.Logging;

namespace Wozzits.Editor.Tests;

/// <summary>
/// D3-P061. FileLogSink's class comment promises that "mirroring to a file can
/// never be the thing that breaks the editor", and the constructor honours it with
/// a bare catch. Write did not: AutoFlush makes every line a real syscall, so a
/// full disk or a removed volume raises IOException out of Write -- which is the
/// FIRST statement of AppendEditorLog, reached from ~60 UI-thread call sites, with
/// no global handler anywhere in the editor to stop it.
/// </summary>
public sealed class FileLogSinkTests
{
    private static void WithTempLogDir(Action<string> test)
    {
        var directory = Path.Combine(
            Path.GetTempPath(),
            "wz-log-sink-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            test(directory);
        }
        finally
        {
            Directory.Delete(directory, recursive: true);
        }
    }

    // The sink holds the file open for WRITE, so File.ReadAllText -- which requests
    // FileShare.Read, excluding the writer -- raises a sharing violation. Reading
    // the live file needs FileShare.ReadWrite.
    private static string ReadWhileOpen(string path)
    {
        using var stream = new FileStream(
            path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }

    // The control. Without it, "Write did not throw" is indistinguishable from a
    // sink that never opened a file and no-ops for an unrelated reason.
    [Fact]
    public void AnOpenSinkWritesTheLine()
    {
        WithTempLogDir(directory =>
        {
            using var sink = new FileLogSink(directory, "control");

            sink.Write("hello from the editor");

            Assert.NotNull(sink.FilePath);
            Assert.Contains("hello from the editor", ReadWhileOpen(sink.FilePath!));
        });
    }

    // Disposing the writer out from under the sink is the cheap stand-in for the
    // real trigger (disk full / volume removed): both make the next Write raise
    // from inside the StreamWriter. ObjectDisposedException is what a disposed
    // writer raises; IOException is what a dead volume raises; the guard catches
    // both, and this is the one of the two a test can produce deterministically.
    [Fact]
    public void AFailingWriteDegradesToANoOpInsteadOfThrowing()
    {
        WithTempLogDir(directory =>
        {
            using var sink = new FileLogSink(directory, "failing");
            var writerField = typeof(FileLogSink).GetField(
                "_writer", BindingFlags.NonPublic | BindingFlags.Instance)!;
            ((StreamWriter)writerField.GetValue(sink)!).Dispose();

            sink.Write("this line cannot be written");

            // Degraded to the same no-op the constructor already degrades to, so
            // every later line is dropped rather than raising again.
            Assert.Null(writerField.GetValue(sink));
            sink.Write("and neither can this one");
        });
    }
}
