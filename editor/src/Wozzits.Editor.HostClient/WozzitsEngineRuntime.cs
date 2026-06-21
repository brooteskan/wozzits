namespace Wozzits.Editor.HostClient;

using System.Runtime.InteropServices;

// Owns the editor's single in-process engine runtime (Option Y, issue #189):
// the engine renders the project's viewport in its own window on an
// engine-owned thread. Started in the constructor; stopped (and joined) on
// Dispose. There is exactly one of these per opened project - one engine.
public sealed class WozzitsEngineRuntime : IDisposable
{
    private readonly Action<string>? _logReceived;
    private readonly WzEditorLogCallback? _logCallback;
    private IntPtr _runtime;

    public WozzitsEngineRuntime(
        string projectDirectory,
        Action<string>? logReceived = null)
    {
        _logReceived = logReceived;
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            Emit("[editor] Resident engine was not started: project directory is empty.");
            return;
        }

        try
        {
            Emit("[editor] Starting resident engine.");
            WozzitsEngineAbi.EnsureResolverRegistered();
            _logCallback = OnNativeLog;
            var callback = Marshal.GetFunctionPointerForDelegate(_logCallback);
            _runtime = WozzitsEngineAbi.WzEditorRuntimeStart(
                projectDirectory,
                resourceRootUtf8: null,
                callback,
                logUser: IntPtr.Zero);
            Emit(_runtime == IntPtr.Zero
                ? "[editor] Resident engine failed to start."
                : "[editor] Resident engine started.");
        }
        catch (DllNotFoundException ex)
        {
            Emit($"[editor] Resident engine failed to start: {ex.Message}");
        }
        catch (EntryPointNotFoundException ex)
        {
            Emit($"[editor] Resident engine failed to start: {ex.Message}");
        }
        catch (BadImageFormatException ex)
        {
            Emit($"[editor] Resident engine failed to start: {ex.Message}");
        }
        catch (InvalidOperationException ex)
        {
            Emit($"[editor] Resident engine failed to start: {ex.Message}");
        }
    }

    public bool IsRunning => _runtime != IntPtr.Zero;

    internal IntPtr Handle => _runtime;

    public void Dispose()
    {
        var runtime = _runtime;
        if (runtime == IntPtr.Zero)
        {
            return;
        }

        _runtime = IntPtr.Zero;
        Emit("[editor] Stopping resident engine.");
        WozzitsEngineAbi.WzEditorRuntimeStop(runtime);
        Emit("[editor] Resident engine stopped.");
    }

    private void OnNativeLog(
        uint level,
        IntPtr timestampUtf8,
        ulong timestampSize,
        IntPtr messageUtf8,
        ulong messageSize,
        IntPtr logUser)
    {
        try
        {
            var timestamp = ReadUtf8(timestampUtf8, timestampSize);
            var message = ReadUtf8(messageUtf8, messageSize);
            var levelName = LevelName(level);
            var prefix = string.IsNullOrWhiteSpace(timestamp)
                ? $"[{levelName}]"
                : $"[{timestamp}] [{levelName}]";
            Emit($"{prefix} [engine] {message}");
        }
        catch
        {
            // Never throw through the native logger callback.
        }
    }

    private void Emit(string line)
    {
        _logReceived?.Invoke(line);
    }

    private static string ReadUtf8(IntPtr value, ulong size)
    {
        if (value == IntPtr.Zero || size == 0)
        {
            return string.Empty;
        }

        return Marshal.PtrToStringUTF8(value, checked((int)size)) ?? string.Empty;
    }

    private static string LevelName(uint level)
    {
        return level switch
        {
            0 => "DEBUG",
            1 => "INFO",
            2 => "WARN",
            3 => "ERROR",
            4 => "CRITICAL",
            _ => "LOG",
        };
    }
}
