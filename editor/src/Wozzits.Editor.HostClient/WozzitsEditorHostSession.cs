using System.Diagnostics;

namespace Wozzits.Editor.HostClient;

public sealed class WozzitsEditorHostSession : IDisposable
{
    private readonly string _hostExecutablePath;
    private Process? _process;
    private bool _disposed;

    public WozzitsEditorHostSession(string? hostExecutablePath = null)
    {
        _hostExecutablePath = string.IsNullOrWhiteSpace(hostExecutablePath)
            ? WozzitsEditorHostClient.ResolveDefaultHostExecutablePath()
            : hostExecutablePath;
    }

    public event Action<string>? LogReceived;

    public bool IsRunning => _process is { HasExited: false };

    public void Start(string projectDirectory)
    {
        if (_disposed)
        {
            throw new ObjectDisposedException(nameof(WozzitsEditorHostSession));
        }

        if (IsRunning)
        {
            return;
        }

        if (!File.Exists(_hostExecutablePath))
        {
            LogReceived?.Invoke(
                $"[error] Editor host executable not found: {_hostExecutablePath}");
            return;
        }

        var startInfo = new ProcessStartInfo
        {
            FileName = _hostExecutablePath,
            UseShellExecute = false,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add("serve");
        startInfo.ArgumentList.Add(projectDirectory);

        try
        {
            var process = new Process
            {
                StartInfo = startInfo,
                EnableRaisingEvents = true,
            };

            process.OutputDataReceived += (_, e) =>
            {
                if (e.Data is not null)
                {
                    LogReceived?.Invoke(e.Data);
                }
            };
            process.ErrorDataReceived += (_, e) =>
            {
                if (e.Data is not null)
                {
                    LogReceived?.Invoke($"[stderr] {e.Data}");
                }
            };
            process.Exited += (_, _) =>
            {
                var exitCode = TryGetExitCode(process);
                LogReceived?.Invoke(
                    exitCode is null
                        ? "[info] editor host exited"
                        : $"[info] editor host exited with code {exitCode}");
            };

            LogReceived?.Invoke(
                $"[info] launching editor host: {_hostExecutablePath}");

            if (!process.Start())
            {
                process.Dispose();
                LogReceived?.Invoke(
                    $"[error] Could not start editor host: {_hostExecutablePath}");
                return;
            }

            _process = process;
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
        }
        catch (Exception ex)
        {
            LogReceived?.Invoke($"[error] {ex.Message}");
        }
    }

    public void Stop()
    {
        var process = _process;
        _process = null;
        if (process is null)
        {
            return;
        }

        try
        {
            if (!process.HasExited)
            {
                process.StandardInput.WriteLine("shutdown");
                process.StandardInput.Flush();

                if (!process.WaitForExit(milliseconds: 2_000))
                {
                    process.Kill(entireProcessTree: true);
                    process.WaitForExit(milliseconds: 2_000);
                }
            }
        }
        catch (Exception ex) when (
            ex is InvalidOperationException
            || ex is IOException
            || ex is ObjectDisposedException)
        {
            LogReceived?.Invoke($"[warn] {ex.Message}");
        }
        finally
        {
            process.Dispose();
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        Stop();
    }

    private static int? TryGetExitCode(Process process)
    {
        try
        {
            return process.ExitCode;
        }
        catch (InvalidOperationException)
        {
            return null;
        }
    }
}
