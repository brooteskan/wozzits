namespace Wozzits.Editor.HostClient;

// Owns the editor's single in-process engine runtime (Option Y, issue #189):
// the engine renders the project's viewport in its own window on an
// engine-owned thread. Started in the constructor; stopped (and joined) on
// Dispose. There is exactly one of these per opened project — one engine.
public sealed class WozzitsEngineRuntime : IDisposable
{
    private IntPtr _runtime;

    public WozzitsEngineRuntime(string projectDirectory)
    {
        if (string.IsNullOrWhiteSpace(projectDirectory))
        {
            return;
        }

        try
        {
            WozzitsEngineAbi.EnsureResolverRegistered();
            _runtime = WozzitsEngineAbi.WzEditorRuntimeStart(projectDirectory, null);
        }
        catch (DllNotFoundException)
        {
        }
        catch (EntryPointNotFoundException)
        {
        }
        catch (BadImageFormatException)
        {
        }
        catch (InvalidOperationException)
        {
        }
    }

    public bool IsRunning => _runtime != IntPtr.Zero;

    public void Dispose()
    {
        var runtime = _runtime;
        if (runtime == IntPtr.Zero)
        {
            return;
        }

        _runtime = IntPtr.Zero;
        WozzitsEngineAbi.WzEditorRuntimeStop(runtime);
    }
}
