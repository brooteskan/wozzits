namespace Wozzits.Editor.HostClient;

using System.Runtime.InteropServices;

// Owns the editor's single in-process engine runtime (Option Y, issue #189):
// the engine renders the project's viewport in its own window on an
// engine-owned thread. Started in the constructor; stopped (and joined) on
// Dispose. IsRunning polls the engine so a closed viewport reads false, and
// Restart() brings it back. There is exactly one of these per opened project.
public sealed class WozzitsEngineRuntime : IDisposable
{
    private readonly Action<string>? _logReceived;
    private readonly string _projectDirectory;
    // Kept alive for the object's lifetime so the native engine can call back
    // across restarts without the delegate being collected.
    private readonly WzEditorLogCallback _logCallback;
    private readonly IntPtr _logCallbackPtr;
    private IntPtr _runtime;
    private bool _disposed;

    public WozzitsEngineRuntime(
        string projectDirectory,
        Action<string>? logReceived = null)
    {
        _logReceived = logReceived;
        _projectDirectory = projectDirectory ?? string.Empty;
        _logCallback = OnNativeLog;
        _logCallbackPtr = Marshal.GetFunctionPointerForDelegate(_logCallback);
        Start();
    }

    // True only while the engine's render thread is actually alive. After the
    // viewport window is closed the native handle lingers (until Dispose/Restart
    // frees it) but the thread is gone, so we poll the engine rather than just
    // checking the handle - otherwise we'd report a dead runtime as running.
    public bool IsRunning =>
        _runtime != IntPtr.Zero
        && WozzitsEngineAbi.WzEditorRuntimeIsRunning(_runtime) != 0;

    internal IntPtr Handle => _runtime;

    // Stop any current runtime (freeing a closed/zombie one) and start a fresh
    // engine + viewport for the same project - the way to bring the viewport
    // back after its window was closed.
    public void Restart()
    {
        if (_disposed)
        {
            return;
        }

        StopNative();
        Start();
    }

    private void Start()
    {
        if (string.IsNullOrWhiteSpace(_projectDirectory))
        {
            Emit("[editor] Resident engine was not started: project directory is empty.");
            return;
        }

        try
        {
            Emit("[editor] Starting resident engine.");
            WozzitsEngineAbi.EnsureResolverRegistered();
            _runtime = WozzitsEngineAbi.WzEditorRuntimeStart(
                _projectDirectory,
                resourceRootUtf8: null,
                _logCallbackPtr,
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

    private void StopNative()
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

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        StopNative();
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
