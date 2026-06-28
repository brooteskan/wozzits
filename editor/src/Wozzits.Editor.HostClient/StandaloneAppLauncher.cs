using System.Diagnostics;
using System.IO;

namespace Wozzits.Editor.HostClient;

// Spawns the project in a SEPARATE process so it plays as the shipped app
// would, distinct from the editor's in-process resident viewport. The editor
// launches the wozzits_editor_host helper (the intended "Launch app" path: it
// resolves the project root to its runtime paths, including the behavior-module
// folder, and spawns wozzits_app_v1.exe). Output is relayed to the editor log.
public sealed class StandaloneAppLauncher
{
    public const string HostExecutableEnvironmentVariable = "WOZZITS_EDITOR_HOST";

    // Resolve wozzits_editor_host.exe. An explicit override wins; otherwise it
    // sits next to the abi DLL the editor already resolves (build/clang-debug),
    // so the two native artifacts stay in lockstep.
    public static string ResolveHostExecutablePath()
    {
        var configured = Environment.GetEnvironmentVariable(
            HostExecutableEnvironmentVariable);
        if (!string.IsNullOrWhiteSpace(configured))
        {
            return configured;
        }

        var abiPath = WozzitsEngineNativeClient.ResolveDefaultAbiPath();
        var directory = Path.GetDirectoryName(abiPath);
        return string.IsNullOrEmpty(directory)
            ? "wozzits_editor_host.exe"
            : Path.Combine(directory, "wozzits_editor_host.exe");
    }

    // Start the standalone play for the given project. Non-blocking: the child
    // runs to its own window's lifetime; stdout/stderr are streamed to log.
    // Returns the started Process so the caller can hold a reference, or null on
    // failure (the reason is sent to log).
    public Process? Launch(string projectDirectory, Action<string> log)
    {
        ArgumentNullException.ThrowIfNull(log);

        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            log("[editor] Cannot launch standalone: no project directory.");
            return null;
        }

        var hostExe = ResolveHostExecutablePath();
        if (!File.Exists(hostExe))
        {
            log($"[editor] Cannot launch standalone: host not found at {hostExe}");
            return null;
        }

        var startInfo = new ProcessStartInfo
        {
            FileName = hostExe,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            // The host reads stdin and treats EOF as a shutdown request. A GUI
            // editor has no console stdin, so an un-redirected child would hit
            // EOF immediately and close the play window the instant it opens.
            // Redirect and hold the stream open; the host then blocks on input
            // and runs until the play window closes (or the editor exits, which
            // closes the pipe and ends the play gracefully).
            RedirectStandardInput = true,
            CreateNoWindow = true,
            WorkingDirectory = Path.GetDirectoryName(hostExe) ?? string.Empty,
        };
        startInfo.ArgumentList.Add("serve");
        startInfo.ArgumentList.Add(projectDirectory);

        var process = new Process
        {
            StartInfo = startInfo,
            EnableRaisingEvents = true,
        };
        process.OutputDataReceived += (_, e) =>
        {
            if (!string.IsNullOrWhiteSpace(e.Data))
            {
                log(e.Data);
            }
        };
        process.ErrorDataReceived += (_, e) =>
        {
            if (!string.IsNullOrWhiteSpace(e.Data))
            {
                log(e.Data);
            }
        };
        process.Exited += (_, _) =>
            log("[editor] Standalone play exited.");

        try
        {
            if (!process.Start())
            {
                log("[editor] Failed to start standalone play.");
                process.Dispose();
                return null;
            }
        }
        catch (Exception ex)
        {
            log($"[editor] Failed to start standalone play: {ex.Message}");
            process.Dispose();
            return null;
        }

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        log("[editor] Launched standalone play.");
        return process;
    }
}
