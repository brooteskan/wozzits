using System.ComponentModel;
using System.Diagnostics;
using System.IO;

namespace Wozzits.Editor.Core.Behaviors;

public enum BehaviorBuildOutcome
{
    // The behavior modules were configured and built successfully.
    Built,

    // There was nothing to build (no behavior/CMakeLists.txt). Not a failure;
    // the caller may still reload to restore built-in behaviors.
    Skipped,

    // A configure/build step failed (or the toolchain could not be launched).
    Failed,
}

// A rebuild's outcome plus the compiler/CMake diagnostics it produced. Every line is
// STILL streamed to `log` (the console keeps the full transcript); this captures just
// the error lines so the UI can put them where the user is actually looking. A failed
// build that only whispers into a busy console reads as "nothing happened" -- and then
// the stale DLL gets blamed on the editor.
public sealed record BehaviorBuildResult(
    BehaviorBuildOutcome Outcome,
    IReadOnlyList<string> Errors)
{
    public static BehaviorBuildResult Of(BehaviorBuildOutcome outcome) =>
        new(outcome, []);
}

// Compiles a project's behavior-module DLLs by driving CMake, mirroring the old
// imgui toolhost editor's rebuild step (cmake --preset / cmake --build, in the
// project's behavior/ folder). All tool output (including compiler errors) is
// streamed line-by-line to `log` so it lands in the editor console. This is the
// editor-side half of the rebuild workflow; reloading the freshly built DLLs
// into the running engine is a separate ABI call.
public sealed class BehaviorModuleBuilder
{
    private const string BehaviorFolderName = "behavior";
    private const string Preset = "clang-debug";
    private const string LogPrefix = "[behavior/build] ";

    // The configure preset's binaryDir (CMakePresets.json:
    // "${sourceDir}/build/cmake-clang-debug"), relative to the behavior folder.
    // This is where CMakeCache.txt lives; the built DLLs go to build/clang-debug.
    private static readonly string ConfigureSubdir =
        Path.Combine("build", "cmake-clang-debug");

    // Cap what we hand the UI: a catastrophic build can emit hundreds of lines, and an
    // error block nobody can scroll past is no better than a log nobody reads.
    private const int MaxCapturedErrors = 40;

    // A compiler/CMake diagnostic worth surfacing.
    //   clang:  foo.cpp:25:13: error: constant expression evaluates to -1 ...
    //   MSVC:   foo.cpp(25,13): error C2440: ...
    //   CMake:  CMake Error at CMakeLists.txt:5 ...
    // Deliberately narrow: ninja's "FAILED:" / "build stopped" summaries and our own
    // "cmake ... failed (exit 1)" line are noise once the real diagnostic is shown.
    internal static bool IsDiagnosticLine(string line) =>
        line.Contains(": error", StringComparison.OrdinalIgnoreCase)
        || line.Contains(": fatal error", StringComparison.OrdinalIgnoreCase)
        || line.StartsWith("CMake Error", StringComparison.OrdinalIgnoreCase);

    public async Task<BehaviorBuildResult> RebuildAsync(
        string projectDirectory,
        Action<string> log,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(log);

        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            log(LogPrefix + "No project directory; skipping behavior rebuild.");
            return BehaviorBuildResult.Of(BehaviorBuildOutcome.Skipped);
        }

        var behaviorDir = Path.Combine(projectDirectory, BehaviorFolderName);
        if (!File.Exists(Path.Combine(behaviorDir, "CMakeLists.txt")))
        {
            log(LogPrefix
                + $"No {BehaviorFolderName}/CMakeLists.txt under {projectDirectory}; nothing to build.");
            return BehaviorBuildResult.Of(BehaviorBuildOutcome.Skipped);
        }

        // Tee: every line still goes to the console verbatim; diagnostics are also kept
        // so a failure can be shown on the behavior card instead of only whispered here.
        var errors = new List<string>();
        void Tee(string line)
        {
            log(line);
            if (errors.Count < MaxCapturedErrors && IsDiagnosticLine(line))
            {
                errors.Add(line);
            }
        }

        // A behavior folder copied from another project (or another machine)
        // brings its build/ cache along, and that cache records the ORIGINAL
        // source/binary directories. CMake then refuses to configure ("does not
        // match the source ... used to generate cache"). Detect that and remove
        // the stale configure tree so the next configure starts clean. The
        // configure tree holds no loaded DLLs (those are emitted to
        // build/clang-debug), so removing it is safe even while the engine runs,
        // and an in-project (non-copied) cache is left intact for incremental
        // rebuilds.
        var configureDir = Path.Combine(behaviorDir, ConfigureSubdir);
        if (IsConfigureCacheForeign(behaviorDir, configureDir))
        {
            log(LogPrefix
                + "CMake cache was generated for a different directory (behavior "
                + "folder copied from another project); reconfiguring from scratch.");
            TryDeleteDirectory(configureDir, log);
        }

        // Configure then build — the same two steps the imgui toolhost ran.
        if (!await RunStepAsync(behaviorDir, $"--preset {Preset}", Tee, cancellationToken)
                .ConfigureAwait(false))
        {
            return new BehaviorBuildResult(BehaviorBuildOutcome.Failed, errors);
        }
        if (!await RunStepAsync(behaviorDir, $"--build --preset {Preset}", Tee, cancellationToken)
                .ConfigureAwait(false))
        {
            return new BehaviorBuildResult(BehaviorBuildOutcome.Failed, errors);
        }

        log(LogPrefix + "Behavior modules built.");
        return BehaviorBuildResult.Of(BehaviorBuildOutcome.Built);
    }

    private static async Task<bool> RunStepAsync(
        string workingDirectory,
        string arguments,
        Action<string> log,
        CancellationToken cancellationToken)
    {
        log(LogPrefix + "cmake " + arguments);

        var startInfo = new ProcessStartInfo
        {
            FileName = "cmake",
            Arguments = arguments,
            WorkingDirectory = workingDirectory,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };

        using var process = new Process { StartInfo = startInfo };

        // Stream stdout and stderr (clang-cl diagnostics arrive on stderr) to the
        // console as they appear.
        process.OutputDataReceived += (_, e) =>
        {
            if (e.Data is not null)
            {
                log(e.Data);
            }
        };
        process.ErrorDataReceived += (_, e) =>
        {
            if (e.Data is not null)
            {
                log(e.Data);
            }
        };

        try
        {
            process.Start();
        }
        catch (Exception ex) when (ex is Win32Exception or InvalidOperationException)
        {
            // Most commonly: cmake (or the clang toolchain it invokes) is not on
            // the editor's PATH because the editor was not launched from a VS
            // Developer environment. Make that explicit rather than silent.
            log(LogPrefix
                + $"Could not run cmake: {ex.Message}. Ensure cmake and the clang "
                + "toolchain are on PATH (launch the editor from a VS Developer "
                + "environment).");
            return false;
        }

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();

        try
        {
            await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            TryKill(process);
            log(LogPrefix + "Build cancelled.");
            return false;
        }

        // Flush any async output still in flight before reading the exit code.
        process.WaitForExit();

        if (process.ExitCode != 0)
        {
            log(LogPrefix + $"cmake {arguments} failed (exit {process.ExitCode}).");
            return false;
        }
        return true;
    }

    // True when the CMake cache in `configureDir` was generated for a different
    // source or binary directory than the current ones — i.e. the behavior
    // folder (with its build/ tree) was copied from another project/machine.
    // Missing cache => not foreign (a fresh configure will create it); an
    // unreadable/malformed cache is treated as foreign (reconfigure to be safe).
    internal static bool IsConfigureCacheForeign(string behaviorDir, string configureDir)
    {
        var cachePath = Path.Combine(configureDir, "CMakeCache.txt");
        if (!File.Exists(cachePath))
        {
            return false;
        }

        string? recordedSource = null;
        string? recordedBinary = null;
        try
        {
            foreach (var line in File.ReadLines(cachePath))
            {
                recordedSource ??= ReadCacheEntry(line, "CMAKE_HOME_DIRECTORY");
                recordedBinary ??= ReadCacheEntry(line, "CMAKE_CACHEFILE_DIR");
                if (recordedSource is not null && recordedBinary is not null)
                {
                    break;
                }
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return true;
        }

        return PathDiffers(recordedSource, behaviorDir)
            || PathDiffers(recordedBinary, configureDir);
    }

    private static string? ReadCacheEntry(string line, string key)
    {
        // Cache lines look like: CMAKE_HOME_DIRECTORY:INTERNAL=D:/path
        if (!line.StartsWith(key + ":", StringComparison.Ordinal))
        {
            return null;
        }
        var equals = line.IndexOf('=');
        return equals >= 0 ? line[(equals + 1)..].Trim() : null;
    }

    private static bool PathDiffers(string? recorded, string expected)
    {
        if (string.IsNullOrWhiteSpace(recorded))
        {
            return true;
        }
        try
        {
            return !string.Equals(
                Normalize(recorded),
                Normalize(expected),
                StringComparison.OrdinalIgnoreCase);
        }
        catch (Exception ex) when (ex is ArgumentException or PathTooLongException)
        {
            return true;
        }

        static string Normalize(string path) =>
            Path.GetFullPath(path).TrimEnd('/', '\\');
    }

    private static void TryDeleteDirectory(string directory, Action<string> log)
    {
        try
        {
            if (Directory.Exists(directory))
            {
                Directory.Delete(directory, recursive: true);
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            log(LogPrefix
                + $"Could not remove stale cache directory '{directory}': {ex.Message}. "
                + "Delete it manually if the configure step fails.");
        }
    }

    private static void TryKill(Process process)
    {
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
            }
        }
        catch (Exception ex) when (ex is InvalidOperationException or Win32Exception)
        {
            // Process already gone; nothing to kill.
        }
    }
}
